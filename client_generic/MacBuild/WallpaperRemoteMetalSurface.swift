import Foundation
import Metal
import QuartzCore

extension CAMetalLayer: @unchecked @retroactive Sendable {}

/// Remote CAContext + CAMetalLayer session that drives the shared dream player.
final class RemoteMetalSurface: @unchecked Sendable {
    enum Role: String {
        case desktop
        case idle
        case settingsPreview
        case unknown
    }

    let wallpaperKey: String
    let role: Role
    let context: CAContext
    let rootLayer: CALayer
    let metalLayer: CAMetalLayer
    let device: MTLDevice
    let commandQueue: MTLCommandQueue
    let adapter: MetalDrawableAdapter
    private(set) var displayIdx: Int32

    private var size: CGSize
    private var scale: CGFloat
    private var timer: DispatchSourceTimer?
    private let renderLock = NSLock()
    private var frameCount: UInt64 = 0
    private var playerStarted = false

    /// Settings System Settings thumbnail — not a live Desktop/Idle surface.
    var isSettingsPreview: Bool { role == .settingsPreview }

    /// Current logical size (points). Used to reject Settings chrome resizes
    /// aimed at a live Desktop session that shares the same wallpaper id.
    var currentSize: CGSize {
        renderLock.lock()
        defer { renderLock.unlock() }
        return size
    }

    init?(
        wallpaperKey: String,
        role: Role,
        size: CGSize,
        scale: CGFloat,
        displayID: UInt32?
    ) {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            extLog("RemoteMetalSurface: Metal device/queue unavailable")
            return nil
        }

        var options: [String: Any] = [:]
        if let displayID { options["displayId"] = NSNumber(value: displayID) }

        let ctx: CAContext
        if options.isEmpty {
            guard let c = CAContext.remoteContext() as? CAContext else { return nil }
            ctx = c
        } else {
            let raw = CAContext.perform(
                NSSelectorFromString("remoteContextWithOptions:"),
                with: options
            )?.takeUnretainedValue()
            guard let c = raw as? CAContext else { return nil }
            ctx = c
        }
        guard ctx.contextId != 0 else { return nil }

        let safeSize = CGSize(
            width: max(size.width, 64),
            height: max(size.height, 64)
        )
        let safeScale = max(scale, 1)

        let root = CALayer()
        root.frame = CGRect(origin: .zero, size: safeSize)
        root.contentsScale = safeScale
        root.backgroundColor = CGColor(gray: 0, alpha: 1)

        let metal = CAMetalLayer()
        metal.device = device
        metal.pixelFormat = .bgra8Unorm
        metal.framebufferOnly = false
        metal.contentsScale = safeScale
        metal.frame = root.bounds
        metal.drawableSize = CGSize(
            width: safeSize.width * safeScale,
            height: safeSize.height * safeScale
        )
        root.addSublayer(metal)

        ctx.layer = root
        CATransaction.flush()

        let adapter = MetalDrawableAdapter(metalLayer: metal, device: device)
        // Settings preview gets preview:true only on a cold engine start;
        // ESScreensaver_Start ignores the flag when Desktop is already live.
        let preview = (role == .settingsPreview)
        let idx = WallpaperPlayerBridge.start(
            with: adapter,
            width: UInt32(safeSize.width),
            height: UInt32(safeSize.height),
            preview: preview
        )
        if idx < 0 {
            extLog("RemoteMetalSurface: WallpaperPlayerBridge start failed")
            ctx.layer = nil
            return nil
        }

        self.wallpaperKey = wallpaperKey
        self.role = role
        self.context = ctx
        self.rootLayer = root
        self.metalLayer = metal
        self.device = device
        self.commandQueue = queue
        self.adapter = adapter
        self.displayIdx = idx
        self.size = safeSize
        self.scale = safeScale
        self.playerStarted = true

        startRenderLoop()
        extLog(
            "RemoteMetalSurface start key=\(wallpaperKey) role=\(role.rawValue) " +
            "preview=\(preview) ctx=\(ctx.contextId) displayIdx=\(idx) " +
            "size=\(Int(safeSize.width))x\(Int(safeSize.height))@\(safeScale)"
        )
    }

    var contextId: UInt32 { context.contextId }

    func updateSize(_ newSize: CGSize, scale newScale: CGFloat) {
        let safeSize = CGSize(width: max(newSize.width, 64), height: max(newSize.height, 64))
        let safeScale = max(newScale, 1)
        renderLock.lock()
        defer { renderLock.unlock() }
        size = safeSize
        scale = safeScale
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        rootLayer.frame = CGRect(origin: .zero, size: safeSize)
        rootLayer.contentsScale = safeScale
        metalLayer.frame = rootLayer.bounds
        metalLayer.contentsScale = safeScale
        metalLayer.drawableSize = CGSize(
            width: safeSize.width * safeScale,
            height: safeSize.height * safeScale
        )
        CATransaction.commit()
        WallpaperPlayerBridge.forceWidth(
            UInt32(safeSize.width),
            height: UInt32(safeSize.height),
            displayIdx: displayIdx
        )
    }

    func invalidate() {
        timer?.cancel()
        timer = nil
        let idx = displayIdx
        if playerStarted {
            playerStarted = false
            displayIdx = -1
            WallpaperPlayerBridge.stopSession(forDisplay: idx)
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        context.layer = nil
        metalLayer.removeFromSuperlayer()
        CATransaction.commit()
        extLog("RemoteMetalSurface invalidate key=\(wallpaperKey) role=\(role.rawValue) ctx=\(contextId)")
    }

    func makeSnapshotXPC() -> AnyObject? {
        renderLock.lock()
        defer { renderLock.unlock() }

        let pixelW = max(Int(metalLayer.drawableSize.width), 64)
        let pixelH = max(Int(metalLayer.drawableSize.height), 64)
        let maxDim = 1280
        let scaleDown = min(1.0, Double(maxDim) / Double(max(pixelW, pixelH)))
        let snapW = max(64, Int(Double(pixelW) * scaleDown))
        let snapH = max(64, Int(Double(pixelH) * scaleDown))

        if let image = Self.makePlaceholderImage(width: snapW, height: snapH, role: role),
           let snapshot = SnapshotIOSurface.make(from: image) {
            return snapshot
        }
        return nil
    }

    private static func makePlaceholderImage(width: Int, height: Int, role: Role) -> CGImage? {
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let ctx = CGContext(
            data: nil,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { return nil }
        switch role {
        case .idle:
            ctx.setFillColor(CGColor(red: 0.10, green: 0.10, blue: 0.18, alpha: 1))
        case .settingsPreview:
            ctx.setFillColor(CGColor(red: 0.08, green: 0.12, blue: 0.16, alpha: 1))
        case .desktop, .unknown:
            ctx.setFillColor(CGColor(red: 0.05, green: 0.08, blue: 0.12, alpha: 1))
        }
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return ctx.makeImage()
    }

    private func startRenderLoop() {
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now(), repeating: 1.0 / 60.0)
        timer.setEventHandler { [weak self] in
            self?.drawFrame()
        }
        timer.resume()
        self.timer = timer
        drawFrame()
    }

    private func drawFrame() {
        renderLock.lock()
        defer { renderLock.unlock() }
        guard playerStarted, displayIdx >= 0 else { return }
        guard metalLayer.drawableSize.width >= 1,
              metalLayer.drawableSize.height >= 1 else { return }
        // Match ESMetalView: acquire drawable before DoFrame so RendererMetal
        // can read -currentDrawable during BeginFrame/EndFrame.
        guard adapter.nextDrawable() != nil else { return }
        WallpaperPlayerBridge.doFrame(displayIdx)
        frameCount &+= 1
    }
}
