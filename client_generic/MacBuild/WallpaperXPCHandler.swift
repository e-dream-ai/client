import AppKit
import Foundation
import Metal
import QuartzCore

extension CALayer: @unchecked @retroactive Sendable {}
extension CAContext: @unchecked @retroactive Sendable {}

/// Milestone 2: Metal remote-CAContext sessions for Desktop and Idle.
final class WallpaperXPCHandler: NSObject, WallpaperExtensionXPCProtocol {
    var agentProxy: (any WallpaperExtensionProxyXPCProtocol)?

    private let lock = NSLock()
    /// Keyed by WallpaperIDXPC identity string. Idle is owned by the screensaver
    /// appex; this handler is Desktop (and Settings preview) only.
    private var sessions: [String: RemoteMetalSurface] = [:]
    private var contextIndex: [UInt32: String] = [:]

    // MARK: - Lifecycle

    func acquire(withId id: Any?, request: Any?, reply: @escaping @Sendable (Any?, (any Error)?) -> Void) {
        let key = Self.wallpaperKey(from: id) ?? UUID().uuidString
        let parsed = Self.parseRequest(request)
        extLog(
            "acquire key=\(key) role=\(parsed.role.rawValue) " +
            "size=\(Int(parsed.size.width))x\(Int(parsed.size.height)) " +
            "scale=\(parsed.scale) display=\(String(describing: parsed.displayID))"
        )

        lock.lock()
        if let existing = sessions[key] {
            existing.invalidate()
            contextIndex.removeValue(forKey: existing.contextId)
            sessions.removeValue(forKey: key)
        }
        lock.unlock()

        guard let surface = RemoteMetalSurface(
            wallpaperKey: key,
            role: parsed.role,
            size: parsed.size,
            scale: parsed.scale,
            displayID: parsed.displayID
        ) else {
            reply(nil, Self.error(2, "RemoteMetalSurface create failed"))
            return
        }

        lock.lock()
        sessions[key] = surface
        contextIndex[surface.contextId] = key
        lock.unlock()

        guard let replyObj = Self.createRemoteContextXPC(contextId: surface.contextId) else {
            surface.invalidate()
            lock.lock()
            sessions.removeValue(forKey: key)
            contextIndex.removeValue(forKey: surface.contextId)
            lock.unlock()
            reply(nil, Self.error(3, "WallpaperRemoteContextXPC create failed"))
            return
        }
        extLog("acquire ok key=\(key) contextId=\(surface.contextId) sessions=\(sessionCount())")
        reply(replyObj, nil)
    }

    func update(withId id: Any?, request: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        let key = Self.wallpaperKey(from: id)
        let parsed = Self.parseRequest(request)
        extLog(
            "update key=\(String(describing: key)) " +
            "size=\(Int(parsed.size.width))x\(Int(parsed.size.height)) scale=\(parsed.scale)"
        )
        lock.lock()
        let surface: RemoteMetalSurface?
        if let key {
            surface = sessions[key]
        } else {
            surface = nil
        }
        lock.unlock()
        if let surface, parsed.size.width >= 1, parsed.size.height >= 1 {
            surface.updateSize(parsed.size, scale: parsed.scale)
        }
        reply(nil)
    }

    func invalidate(withId id: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        let key = Self.wallpaperKey(from: id)
        extLog("invalidate key=\(String(describing: key)) id=\(String(describing: id))")
        lock.lock()
        let surface: RemoteMetalSurface?
        if let key, let s = sessions.removeValue(forKey: key) {
            contextIndex.removeValue(forKey: s.contextId)
            surface = s
        } else if let ctx = Self.contextId(from: id),
                  let k = contextIndex.removeValue(forKey: ctx),
                  let s = sessions.removeValue(forKey: k) {
            surface = s
        } else {
            surface = nil
        }
        lock.unlock()
        surface?.invalidate()
        extLog("invalidate done sessions=\(sessionCount())")
        reply(nil)
    }

    func snapshot(withId id: Any?, reply: @escaping @Sendable (Any?, (any Error)?) -> Void) {
        let key = Self.wallpaperKey(from: id)
        extLog("snapshot key=\(String(describing: key))")
        lock.lock()
        let surface: RemoteMetalSurface?
        if let key {
            surface = sessions[key]
        } else if let ctx = Self.contextId(from: id),
                  let k = contextIndex[ctx] {
            surface = sessions[k]
        } else {
            // Settings may snapshot before acquire; use any live session or synthesize.
            surface = sessions.values.first
        }
        lock.unlock()

        if let snapshot = surface?.makeSnapshotXPC() {
            reply(snapshot, nil)
            return
        }

        // Fallback: teal CGImage → global IOSurface so Settings still gets a tile.
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        if let ctx = CGContext(
            data: nil,
            width: 640,
            height: 360,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) {
            ctx.setFillColor(CGColor(red: 0.05, green: 0.55, blue: 0.60, alpha: 1))
            ctx.fill(CGRect(x: 0, y: 0, width: 640, height: 360))
            if let image = ctx.makeImage(),
               let snapshot = SnapshotIOSurface.make(from: image) {
                extLog("snapshot fallback synthetic cg")
                reply(snapshot, nil)
                return
            }
        }

        reply(nil, Self.error(2, "snapshot unavailable"))
    }

    // MARK: - Settings

    func provideSettingsViewModels(
        withContentTypes types: Any?,
        reply: @escaping @Sendable (Any?, (any Error)?) -> Void
    ) {
        extLog("provideSettingsViewModels types=\(String(describing: types))")
        reply(buildSettingsViewModelsXPC() ?? makeEmptyGroupsResponse(), nil)
    }

    // MARK: - Stubs

    func addChoiceRequest(
        withChoiceRequest _: Any?,
        onBehalfOfProcess _: Any?,
        reply: @escaping @Sendable (Any?, (any Error)?) -> Void
    ) { reply(nil, nil) }

    func removeChoiceRequest(withChoiceRequest _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        reply(nil)
    }

    func selectedChoicesDidChange(for _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        reply(nil)
    }

    func invokeContextMenuAction(
        withMenuItemID _: Any?,
        groupItemID _: Any?,
        reply: @escaping @Sendable ((any Error)?) -> Void
    ) { reply(nil) }

    func isChoiceDownloaded(with _: Any?, reply: @escaping @Sendable (Bool, (any Error)?) -> Void) {
        reply(true, nil)
    }

    func download(withChoiceID _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) -> Any? {
        reply(nil)
        return nil
    }

    func pauseDownload(for _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) { reply(nil) }
    func cancelDownload(for _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) { reply(nil) }
    func resumeDownload(for _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) { reply(nil) }
    func removeDownload(for _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) { reply(nil) }

    func migrateSelectedChoice(for _: Any?, reply: @escaping @Sendable (Any?, (any Error)?) -> Void) {
        reply(nil, nil)
    }

    func migrate(from _: Any?, to _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        reply(nil)
    }

    func skipShuffledContent(withId _: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        reply(nil)
    }

    func canSkipShuffledContent(withId _: Any?, reply: @escaping @Sendable (Bool, (any Error)?) -> Void) {
        reply(false, nil)
    }

    func handleDebugRequest(for _: Any?, reply: @escaping @Sendable (Any?, (any Error)?) -> Void) {
        reply(nil, nil)
    }

    func handleNotification(withNamed name: Any?, reply: @escaping @Sendable ((any Error)?) -> Void) {
        extLog("handleNotification \(String(describing: name))")
        reply(nil)
    }

    // MARK: - Helpers

    private func sessionCount() -> Int {
        lock.lock()
        defer { lock.unlock() }
        return sessions.count
    }

    private struct ParsedRequest {
        var displayID: UInt32?
        var size: CGSize = CGSize(width: 1920, height: 1080)
        var scale: CGFloat = 2
        var role: RemoteMetalSurface.Role = .unknown
    }

    private static func parseRequest(_ request: Any?) -> ParsedRequest {
        var parsed = ParsedRequest()
        guard let request else { return parsed }
        let desc = String(describing: request)

        if let range = desc.range(of: "displayId: Optional(") {
            let after = desc[range.upperBound...]
            if let end = after.range(of: ")") {
                parsed.displayID = UInt32(after[..<end.lowerBound].trimmingCharacters(in: .whitespaces))
            }
        }
        if let range = desc.range(of: "destinationSize: NSSize(width: ") {
            let afterW = desc[range.upperBound...]
            if let endW = afterW.range(of: ", height: "),
               let endH = afterW[endW.upperBound...].range(of: ")") {
                parsed.size.width = CGFloat(Double(afterW[..<endW.lowerBound]) ?? 1920)
                parsed.size.height = CGFloat(Double(afterW[endW.upperBound..<endH.lowerBound]) ?? 1080)
            }
        }
        if let range = desc.range(of: "scaleFactor: ") {
            let after = desc[range.upperBound...]
            if let end = after.range(of: ",") ?? after.range(of: ")") {
                parsed.scale = CGFloat(Double(after[..<end.lowerBound].trimmingCharacters(in: .whitespaces)) ?? 2)
            }
        }

        let lower = desc.lowercased()
        if lower.contains("screensaver") || lower.contains("screen_saver") || lower.contains("idle") {
            parsed.role = .idle
        } else if lower.contains("desktop") || lower.contains("wallpaper") {
            parsed.role = .desktop
        }

        if let did = parsed.displayID {
            let w = CGDisplayPixelsWide(CGDirectDisplayID(did))
            let h = CGDisplayPixelsHigh(CGDirectDisplayID(did))
            if w > 0, h > 0, parsed.size.width < 2 || parsed.size.height < 2 {
                parsed.size = CGSize(width: w, height: h)
                parsed.scale = 1
            }
        }
        return parsed
    }

    private static func wallpaperKey(from id: Any?) -> String? {
        guard let id else { return nil }
        if let s = id as? String { return s }
        if let n = id as? NSNumber { return n.stringValue }
        let mirror = Mirror(reflecting: id)
        for child in mirror.children {
            if let s = child.value as? String, !s.isEmpty { return s }
            if let n = child.value as? NSNumber { return n.stringValue }
            if let u = child.value as? UUID { return u.uuidString }
            let nested = Mirror(reflecting: child.value)
            for nchild in nested.children {
                if let s = nchild.value as? String, !s.isEmpty { return s }
                if let u = nchild.value as? UUID { return u.uuidString }
                if let n = nchild.value as? NSNumber { return n.stringValue }
            }
        }
        // Stable fallback from description so Desktop/Idle don't collide randomly.
        let desc = String(describing: id)
        if desc.count < 200 { return desc }
        return String(desc.prefix(200))
    }

    private static func createRemoteContextXPC(contextId: UInt32) -> AnyObject? {
        guard let realClass = objc_getClass("WallpaperRemoteContextXPC") as? AnyClass,
              let raw = class_createInstance(realClass, 0) else {
            extLog("WallpaperRemoteContextXPC class missing")
            return nil
        }
        let obj = raw as AnyObject
        if obj.responds(to: NSSelectorFromString("setBox:"))
            || obj.responds(to: NSSelectorFromString("setContextId:")) {
            obj.setValue(NSNumber(value: contextId), forKey: "box")
            return obj
        }
        guard let ivar = class_getInstanceVariable(realClass, "box") else {
            return obj
        }
        let offset = ivar_getOffset(ivar)
        let size = class_getInstanceSize(realClass)
        guard offset + MemoryLayout<UInt32>.size <= size else { return obj }
        Unmanaged.passUnretained(obj).toOpaque()
            .advanced(by: offset)
            .storeBytes(of: contextId, as: UInt32.self)
        return obj
    }

    private static func contextId(from id: Any?) -> UInt32? {
        guard let id else { return nil }
        if let n = id as? NSNumber { return n.uint32Value }
        let mirror = Mirror(reflecting: id)
        for child in mirror.children {
            if child.label == "box" || child.label == "contextId",
               let n = child.value as? UInt32 {
                return n
            }
            if let n = child.value as? NSNumber { return n.uint32Value }
        }
        return nil
    }

    private static func error(_ code: Int, _ message: String) -> NSError {
        NSError(domain: "InfinidreamWallpaper", code: code,
                userInfo: [NSLocalizedDescriptionKey: message])
    }
}
