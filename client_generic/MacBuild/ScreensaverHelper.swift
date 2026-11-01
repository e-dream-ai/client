//
//  ScreensaverHelper.swift
//  infinidream
//
//  Objective-C bridge for PaperSaver screensaver + wallpaper APIs.
//

import Foundation
import PaperSaverKit

@objc public class ScreensaverHelper: NSObject {
    private lazy var paperSaver: PaperSaver = PaperSaver()

    @objc nonisolated(unsafe) public static let shared = ScreensaverHelper()

    private override init() {
        super.init()
    }

    /// Module stem (PRODUCT_NAME / .appex file stem) used by ModuleLocator and
    /// `setScreensaver(module:)`.
    private var moduleName: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage") ? "infinidream-stage" : "infinidream"
    }

    /// Bundle ID of the embedded screensaver appex (must match PRODUCT_BUNDLE_IDENTIFIER).
    private var extensionBundleID: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage")
            ? "ai.infinidream.stage.app.screensaver"
            : "ai.infinidream.alpha.app.screensaver"
    }

    /// Appex file name inside the app's PlugIns folder.
    private var embeddedAppexName: String {
        "\(moduleName).appex"
    }

    /// URL of the appex embedded in the running app bundle.
    private var embeddedAppexURL: URL? {
        guard let plugins = Bundle.main.builtInPlugInsURL else { return nil }
        let url = plugins.appendingPathComponent(embeddedAppexName)
        return FileManager.default.fileExists(atPath: url.path) ? url : nil
    }

    /// `~/Library/Screen Savers/<module>.appex`. Earlier builds symlinked the
    /// appex here; WallpaperAgent rejects a module URL that points at it.
    private var userScreenSaversAppexURL: URL? {
        guard let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first else {
            return nil
        }
        return library
            .appendingPathComponent("Screen Savers", isDirectory: true)
            .appendingPathComponent(embeddedAppexName)
    }

    // MARK: - Screensaver (Idle slot)

    /// True when Idle points at our module (screen-saver choice) or, legacy,
    /// a raw wallpaper-extension Provider with our bundle ID.
    @objc public func isInfinidreamActive() -> Bool {
        if idlePointsAtInfinidream() {
            return true
        }
        if #available(macOS 14.0, *),
           paperSaver.getCurrentScreensaverExtension() == extensionBundleID {
            return true
        }
        let active = paperSaver.getActiveScreensavers()
        return active.contains(moduleName) || active.contains(extensionBundleID)
    }

    /// Set our appex as the system screensaver (Idle slot).
    ///
    /// Idle must use `com.apple.wallpaper.choice.screen-saver` with a module URL
    /// that is the real embedded `.appex` (no symlink). Writing our bundle ID via
    /// `setWallpaperExtension(slot: .screensaver)` is reset to `Provider=default`
    /// by WallpaperAgent, and `setScreensaver(module:)` can only resolve modules
    /// by name through standard folders / PlugInKit.
    @objc public func setInfinidreamAsActive(completion: @escaping (Bool, Error?) -> Void) {
        Task {
            do {
                try await writeScreenSaverModuleChoice(slots: [.screensaver])
                await MainActor.run { completion(true, nil) }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    /// Check if any screensaver (not just infinidream) is active
    @objc public func hasActiveScreensaver() -> Bool {
        if paperSaver.activeScreensaver() != nil {
            return true
        }
        return !paperSaver.getActiveScreensavers().isEmpty
            || paperSaver.getCurrentScreensaverExtension() != nil
    }

    /// Get the name of the first currently active screensaver
    @objc public func getActiveScreensaverName() -> String? {
        if let info = paperSaver.activeScreensaver() {
            return info.name
        }
        if #available(macOS 14.0, *),
           let bid = paperSaver.getCurrentScreensaverExtension() {
            return bid
        }
        return paperSaver.getActiveScreensavers().first
    }

    // MARK: - Wallpaper (Desktop slot)

    /// True when Desktop is a screen-saver choice whose module is our appex.
    ///
    /// A raw Provider equal to our bundle ID is NOT treated as active: WallpaperAgent
    /// fails `makeWallpaper` for screensaver-point appexes (`no provider found`) and
    /// falls back to Aerials while leaving that Provider in the store.
    @objc public func isInfinidreamWallpaperActive() -> Bool {
        guard #available(macOS 14.0, *) else { return false }
        return desktopPointsAtInfinidream()
    }

    /// Set our appex as the live desktop wallpaper on every display/Space.
    ///
    /// Desktop must use the same `com.apple.wallpaper.choice.screen-saver` + module
    /// URL shape as Idle. `setWallpaperExtension` writes a wallpaper-extension
    /// Provider, which WallpaperAgent cannot resolve for a `com.apple.screensaver` appex.
    @objc public func setInfinidreamAsWallpaper(completion: @escaping (Bool, Error?) -> Void) {
        Task {
            do {
                if #available(macOS 14.0, *) {
                    try await writeScreenSaverModuleChoice(slots: [.desktop])
                    await MainActor.run { completion(true, nil) }
                } else {
                    await MainActor.run {
                        completion(
                            false,
                            NSError(
                                domain: "ScreensaverHelper",
                                code: 1,
                                userInfo: [
                                    NSLocalizedDescriptionKey:
                                        "Live wallpaper requires macOS 14 or later"
                                ]
                            )
                        )
                    }
                }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    /// Set our appex as desktop wallpaper and Idle screensaver in one store write.
    @objc public func setInfinidreamAsWallpaperAndScreensaver(
        completion: @escaping (Bool, Error?) -> Void
    ) {
        Task {
            do {
                if #available(macOS 14.0, *) {
                    try await writeScreenSaverModuleChoice(slots: [.desktop, .screensaver])
                } else {
                    try await writeScreenSaverModuleChoice(slots: [.screensaver])
                }
                await MainActor.run { completion(true, nil) }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    // MARK: - Slot helpers

    /// True when the wallpaper store Idle slot selects our appex/module.
    private func idlePointsAtInfinidream() -> Bool {
        if let info = paperSaver.activeScreensaver() {
            if info.name == moduleName || info.identifier == embeddedAppexName {
                return true
            }
            if info.provider == extensionBundleID {
                return true
            }
            if let path = info.modulePath, modulePathPointsAtUs(path) {
                return true
            }
        }
        if slotPointsAtOurScreenSaverModule(.screensaver) {
            return true
        }
        let names = paperSaver.getActiveScreensavers()
        return names.contains(moduleName) || names.contains(extensionBundleID)
    }

    /// True when Desktop is a screen-saver choice whose module URL is our appex.
    private func desktopPointsAtInfinidream() -> Bool {
        slotPointsAtOurScreenSaverModule(.desktop)
    }

    private func slotPointsAtOurScreenSaverModule(_ slot: WallpaperSlot) -> Bool {
        guard let choice = paperSaver.currentWallpaperChoice(slot: slot),
              choice.provider == .screenSaver,
              case .screenSaver(let url)? = choice.configuration else {
            return false
        }
        return modulePathPointsAtUs(url.path)
    }

    private func modulePathPointsAtUs(_ path: String) -> Bool {
        if path.hasSuffix("/\(embeddedAppexName)") || path.contains("/\(embeddedAppexName)/") {
            return true
        }
        if let embedded = embeddedAppexURL?.resolvingSymlinksInPath(),
           path == embedded.path {
            return true
        }
        return false
    }

    /// Write Desktop and/or Idle as screen-saver choices whose module URL is the
    /// embedded appex, restart WallpaperAgent, and confirm it kept the choice(s).
    /// Restores the previous store when the agent drops them.
    private func writeScreenSaverModuleChoice(slots: Set<WallpaperSlot>) async throws {
        guard !slots.isEmpty else {
            throw Self.error(4, "No wallpaper slots to write")
        }
        guard let embedded = embeddedAppexURL?.resolvingSymlinksInPath() else {
            throw Self.error(3, "Embedded screensaver appex not found in app PlugIns")
        }
        removeLegacyScreenSaversSymlink()

        // System Settings only highlights the selected tile when the module URL has
        // no trailing slash and option values are `{values: {}}`, as it writes them.
        let appexURL = URL(fileURLWithPath: embedded.path, isDirectory: false)
        let file = StoreFile(url: paperSaver.configuration.storeURL)
        let moduleChoice = SlotConfiguration(
            choice: try WallpaperChoice(provider: .screenSaver,
                                        configuration: .screenSaver(moduleURL: appexURL)),
            optionValues: .values(.empty), shuffle: .null)

        var updated = try file.read()
        let ordered = WallpaperSlot.allCases.filter(slots.contains)
        for slot in ordered {
            updated = try updated.setting(
                slot, to: moduleChoice, in: .everywhere, otherSlot: .keep,
                strategy: paperSaver.configuration.screensaverOnlyStrategy,
                systemDefault: paperSaver.configuration.systemDefault)
        }
        let snapshot = try file.write(updated)

        let agent = WallpaperAgent()
        try await agent.restart()
        try? await agent.waitForRespawn(timeout: .seconds(5))
        // The agent rewrites the store shortly after launch when it rejects a choice.
        try await Task.sleep(for: .seconds(2))

        var failed: [String] = []
        if slots.contains(.screensaver), !idlePointsAtInfinidream() {
            failed.append("Idle")
        }
        if slots.contains(.desktop), !desktopPointsAtInfinidream() {
            failed.append("Desktop")
        }
        if !failed.isEmpty {
            try? file.restore(snapshot)
            try? await agent.restart()
            throw Self.error(
                2,
                "WallpaperAgent did not keep the \(failed.joined(separator: "+")) choice for \(appexURL.path)")
        }
    }

    /// Earlier builds symlinked the appex into `~/Library/Screen Savers`; remove
    /// that link so ModuleLocator and System Settings stop resolving to it.
    private func removeLegacyScreenSaversSymlink() {
        guard let link = userScreenSaversAppexURL,
              (try? FileManager.default.destinationOfSymbolicLink(atPath: link.path)) != nil else {
            return
        }
        try? FileManager.default.removeItem(at: link)
    }

    private static func error(_ code: Int, _ message: String) -> NSError {
        NSError(domain: "ScreensaverHelper", code: code,
                userInfo: [NSLocalizedDescriptionKey: message])
    }
}
