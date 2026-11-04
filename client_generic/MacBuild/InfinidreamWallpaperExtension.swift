import ExtensionFoundation
import Foundation
import ObjectiveC

struct WallpaperExtensionConfiguration: AppExtensionConfiguration {
    func accept(connection: NSXPCConnection) -> Bool {
        extLog("XPC accept from pid=\(connection.processIdentifier)")

        let exported = NSXPCInterface(with: WallpaperExtensionXPCProtocol.self)
        let typeNames = [
            "WallpaperIDXPC",
            "WallpaperCreationRequestXPC",
            "WallpaperUpdateRequestXPC",
            "WallpaperRemoteContextXPC",
            "WallpaperSnapshotXPC",
            "WallpaperContentTypeSetXPC",
            "WallpaperChoiceIDXPC",
            "WallpaperChoiceIDsXPC",
            "WallpaperExtensionChoiceRequestXPC",
            "WallpaperChoiceRequestAdditionResultXPC",
            "WallpaperDebugRequestXPC",
            "WallpaperDebugResponseXPC",
            "WallpaperMigrationVersionXPC",
            "WallpaperSettingsViewModelsXPC",
            "AuditTokenXPC",
        ]

        let allTypes = NSMutableSet()
        var missing: [String] = []
        for name in typeNames {
            if let cls = objc_getClass(name) {
                allTypes.add(cls)
            } else {
                missing.append(name)
            }
        }
        if !missing.isEmpty {
            extLog("MISSING XPC types: \(missing.joined(separator: ", "))")
        }
        allTypes.add(NSString.self)
        allTypes.add(NSNumber.self)
        allTypes.add(NSData.self)
        allTypes.add(NSArray.self)
        allTypes.add(NSDictionary.self)
        allTypes.add(NSURL.self)
        allTypes.add(NSError.self)

        let classes = allTypes as! Set<AnyHashable>
        let selectors: [(Selector, Int, Bool)] = [
            (#selector(WallpaperXPCHandler.acquire(withId:request:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.acquire(withId:request:reply:)), 1, false),
            (#selector(WallpaperXPCHandler.acquire(withId:request:reply:)), 0, true),
            (#selector(WallpaperXPCHandler.update(withId:request:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.update(withId:request:reply:)), 1, false),
            (#selector(WallpaperXPCHandler.invalidate(withId:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.snapshot(withId:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.snapshot(withId:reply:)), 0, true),
            (#selector(WallpaperXPCHandler.provideSettingsViewModels(withContentTypes:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.provideSettingsViewModels(withContentTypes:reply:)), 0, true),
            (#selector(WallpaperXPCHandler.addChoiceRequest(withChoiceRequest:onBehalfOfProcess:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.addChoiceRequest(withChoiceRequest:onBehalfOfProcess:reply:)), 1, false),
            (#selector(WallpaperXPCHandler.removeChoiceRequest(withChoiceRequest:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.selectedChoicesDidChange(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.invokeContextMenuAction(withMenuItemID:groupItemID:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.invokeContextMenuAction(withMenuItemID:groupItemID:reply:)), 1, false),
            (#selector(WallpaperXPCHandler.isChoiceDownloaded(with:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.download(withChoiceID:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.pauseDownload(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.cancelDownload(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.resumeDownload(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.removeDownload(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.migrateSelectedChoice(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.migrateSelectedChoice(for:reply:)), 0, true),
            (#selector(WallpaperXPCHandler.migrate(from:to:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.migrate(from:to:reply:)), 1, false),
            (#selector(WallpaperXPCHandler.skipShuffledContent(withId:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.canSkipShuffledContent(withId:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.handleDebugRequest(for:reply:)), 0, false),
            (#selector(WallpaperXPCHandler.handleDebugRequest(for:reply:)), 0, true),
            (#selector(WallpaperXPCHandler.handleNotification(withNamed:reply:)), 0, false),
        ]
        for (sel, idx, isReply) in selectors {
            exported.setClasses(classes, for: sel, argumentIndex: idx, ofReply: isReply)
        }

        connection.exportedInterface = exported
        connection.remoteObjectInterface = NSXPCInterface(with: WallpaperExtensionProxyXPCProtocol.self)

        let handler = WallpaperXPCHandler()
        connection.exportedObject = handler
        // Only drop Settings preview surfaces on XPC blips — keep live Desktop.
        connection.interruptionHandler = {
            extLog("XPC interrupted pid=\(connection.processIdentifier) — drop Settings preview only")
            handler.invalidateSettingsPreviewSessions()
        }
        connection.invalidationHandler = {
            extLog("XPC invalidated pid=\(connection.processIdentifier) — drop Settings preview only")
            handler.invalidateSettingsPreviewSessions()
            handler.agentProxy = nil
        }
        connection.resume()
        handler.agentProxy = connection.remoteObjectProxy as? WallpaperExtensionProxyXPCProtocol
        extLog("XPC accepted")
        return true
    }
}

@main
final class InfinidreamWallpaperExtension: NSObject, AppExtension {
    typealias Configuration = WallpaperExtensionConfiguration

    var configuration: WallpaperExtensionConfiguration {
        WallpaperExtensionConfiguration()
    }

    override required init() {
        super.init()
        let path =
            "/System/Library/PrivateFrameworks/WallpaperExtensionKit.framework/WallpaperExtensionKit"
        if let handle = dlopen(path, RTLD_LAZY) {
            _ = handle
            extLog("INIT pid=\(ProcessInfo.processInfo.processIdentifier) WallpaperExtensionKit loaded")
            Self.swizzleSnapshotEncodeIfNeeded()
        } else {
            let err = String(cString: dlerror())
            extLog("INIT dlopen failed: \(err)")
        }
        // WallpaperTypes is needed for settings view-model encoding remap.
        _ = dlopen(
            "/System/Library/PrivateFrameworks/WallpaperTypes.framework/WallpaperTypes",
            RTLD_LAZY
        )
    }

    private static func swizzleSnapshotEncodeIfNeeded() {
        guard let snapshotClass = NSClassFromString("WallpaperSnapshotXPC") else {
            NSLog("[infinidream-wallpaper] swizzle: WallpaperSnapshotXPC missing")
            return
        }
        let sel = NSSelectorFromString("encodeWithCoder:")
        guard let origMethod = class_getInstanceMethod(snapshotClass, sel) else { return }
        let origIMP = method_getImplementation(origMethod)
        typealias EncodeFunc = @convention(c) (AnyObject, Selector, NSCoder) -> Void
        let origFunc = unsafeBitCast(origIMP, to: EncodeFunc.self)
        // Prefer the concrete encoder class when present — forcing the abstract
        // NSXPCCoder base can produce payloads that initWithCoder rejects.
        let preferredName = NSClassFromString("NSXPCEncoder") != nil ? "NSXPCEncoder" : "NSXPCCoder"
        guard let preferredClass = NSClassFromString(preferredName) else { return }
        let block: @convention(block) (AnyObject, NSCoder) -> Void = { obj, coder in
            let origClass: AnyClass = object_getClass(coder)!
            NSLog(
                "[infinidream-wallpaper] encodeWithCoder coder=%@ -> %@",
                String(describing: origClass),
                preferredName
            )
            object_setClass(coder, preferredClass)
            origFunc(obj, sel, coder)
            object_setClass(coder, origClass)
        }
        method_setImplementation(origMethod, imp_implementationWithBlock(block))
        NSLog("[infinidream-wallpaper] swizzled WallpaperSnapshotXPC encodeWithCoder -> %@", preferredName)
        extLog("swizzled WallpaperSnapshotXPC encodeWithCoder: -> \(preferredName)")
    }
}
