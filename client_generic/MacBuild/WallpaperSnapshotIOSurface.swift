import CoreGraphics
import Foundation
import IOSurface
import Metal
import ObjectiveC

/// Builds WallpaperSnapshotXPC wrapping a global IOSurface (required for NSXPC).
enum SnapshotIOSurface {
    static func make(
        width: Int,
        height: Int,
        device: MTLDevice,
        commandQueue: MTLCommandQueue,
        paint: (_ encoder: MTLRenderCommandEncoder, _ width: Int, _ height: Int) -> Void
    ) -> AnyObject? {
        guard width > 0, height > 0,
              let snapshotClass = objc_getClass("WallpaperSnapshotXPC") as? AnyClass else {
            return nil
        }

        let bytesPerRow = align(width * 4, to: max(IOSurfaceGetPropertyAlignment(kIOSurfaceBytesPerRow), 16))
        let allocSize = align(bytesPerRow * height, to: max(IOSurfaceGetPropertyAlignment(kIOSurfaceAllocSize), 16))
        var props: [IOSurfacePropertyKey: Any] = [
            .width: width,
            .height: height,
            .bytesPerElement: 4,
            .bytesPerRow: bytesPerRow,
            .allocSize: allocSize,
            .pixelFormat: 0x4247_5241, // BGRA
        ]
        // XPC transfer requires a global IOSurface (WaifuX).
        props[IOSurfacePropertyKey(rawValue: "IOSurfaceIsGlobal")] = true

        guard let surface = IOSurface(properties: props) else {
            extLog("SnapshotIOSurface: IOSurface create failed")
            return nil
        }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        descriptor.usage = [.renderTarget, .shaderRead]
        descriptor.storageMode = .shared

        guard let texture = device.makeTexture(
            descriptor: descriptor,
            iosurface: surface,
            plane: 0
        ),
        let commandBuffer = commandQueue.makeCommandBuffer() else {
            return nil
        }

        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = texture
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0.05, green: 0.55, blue: 0.60, alpha: 1)

        if let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: pass) {
            paint(encoder, width, height)
            encoder.endEncoding()
        }
        commandBuffer.commit()
        commandBuffer.waitUntilCompleted()

        return wrap(surface: surface, snapshotClass: snapshotClass)
    }

    /// CPU path: paint a CGImage into a global IOSurface (most reliable for Settings).
    static func make(from image: CGImage) -> AnyObject? {
        guard let snapshotClass = objc_getClass("WallpaperSnapshotXPC") as? AnyClass else {
            return nil
        }
        let width = image.width
        let height = image.height
        guard width > 0, height > 0 else { return nil }

        let bytesPerRow = align(width * 4, to: max(IOSurfaceGetPropertyAlignment(kIOSurfaceBytesPerRow), 16))
        let allocSize = align(bytesPerRow * height, to: max(IOSurfaceGetPropertyAlignment(kIOSurfaceAllocSize), 16))
        var props: [IOSurfacePropertyKey: Any] = [
            .width: width,
            .height: height,
            .bytesPerElement: 4,
            .bytesPerRow: bytesPerRow,
            .allocSize: allocSize,
            .pixelFormat: 0x4247_5241,
        ]
        props[IOSurfacePropertyKey(rawValue: "IOSurfaceIsGlobal")] = true

        guard let surface = IOSurface(properties: props) else { return nil }
        surface.lock(options: [], seed: nil)
        defer { surface.unlock(options: [], seed: nil) }

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let ctx = CGContext(
            data: surface.baseAddress,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: surface.bytesPerRow,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue
        ) else {
            return nil
        }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        return wrap(surface: surface, snapshotClass: snapshotClass)
    }

    static func wrap(surface: IOSurface, snapshotClass: AnyClass) -> AnyObject? {
        let instanceSize = class_getInstanceSize(snapshotClass)
        guard instanceSize >= 8 + MemoryLayout<UnsafeMutableRawPointer?>.size else {
            extLog("SnapshotIOSurface: unexpected instanceSize=\(instanceSize)")
            return nil
        }
        // Do NOT call -init: WallpaperSnapshotXPC.init traps (EXC_BREAKPOINT).
        // WaifuX/Aerial poke rawValue onto a class_createInstance shell.
        guard let raw = class_createInstance(snapshotClass, 0) else {
            extLog("SnapshotIOSurface: WallpaperSnapshotXPC alloc failed")
            return nil
        }
        let instance = raw as AnyObject
        let surfaceRef = Unmanaged.passRetained(surface).toOpaque()
        let instancePtr = Unmanaged.passUnretained(instance).toOpaque()
        instancePtr.advanced(by: 8).storeBytes(of: surfaceRef, as: UnsafeMutableRawPointer.self)
        return instance
    }

    private static func align(_ value: Int, to alignment: Int) -> Int {
        let a = max(alignment, 1)
        return (value + a - 1) / a * a
    }
}
