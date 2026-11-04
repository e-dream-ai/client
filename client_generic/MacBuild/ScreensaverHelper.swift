//
//  ScreensaverHelper.swift
//  infinidream
//
//  Objective-C bridge for PaperSaver screensaver + wallpaper APIs.
//
//  Desktop and Idle are separate processes:
//  - Desktop → wallpaper appex (com.apple.wallpaper)
//  - Idle    → screensaver appex (com.apple.screensaver)
//

import Foundation
import PaperSaverKit

@objc public class ScreensaverHelper: NSObject {
    private lazy var paperSaver: PaperSaver = {
        var config = PaperSaverConfiguration()
        config.validatesWallpaperExtensions = true
        return PaperSaver(configuration: config)
    }()

    @objc nonisolated(unsafe) public static let shared = ScreensaverHelper()

    private override init() {
        super.init()
    }

    /// Module stem (PRODUCT_NAME / .appex file stem) used by ModuleLocator.
    private var moduleName: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage") ? "infinidream-stage" : "infinidream"
    }

    /// Bundle ID of the Idle screensaver appex (com.apple.screensaver).
    private var screensaverBundleID: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage")
            ? "ai.infinidream.stage.app.screensaver"
            : "ai.infinidream.alpha.app.screensaver"
    }

    /// Bundle ID of the Desktop wallpaper appex (com.apple.wallpaper).
    private var wallpaperBundleID: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage")
            ? "ai.infinidream.stage.app.wallpaper"
            : "ai.infinidream.alpha.app.wallpaper"
    }

    private var embeddedScreensaverAppexName: String { "\(moduleName).appex" }

    private var embeddedWallpaperAppexName: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage")
            ? "infinidream-stage-wallpaper.appex"
            : "infinidream-wallpaper.appex"
    }

    private var embeddedScreensaverURL: URL? {
        guard let plugins = Bundle.main.builtInPlugInsURL else { return nil }
        let url = plugins.appendingPathComponent(embeddedScreensaverAppexName)
        return FileManager.default.fileExists(atPath: url.path) ? url : nil
    }

    private var embeddedWallpaperURL: URL? {
        guard let plugins = Bundle.main.builtInPlugInsURL else { return nil }
        let url = plugins.appendingPathComponent(embeddedWallpaperAppexName)
        return FileManager.default.fileExists(atPath: url.path) ? url : nil
    }

    private var userScreenSaversAppexURL: URL? {
        guard let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first else {
            return nil
        }
        return library
            .appendingPathComponent("Screen Savers", isDirectory: true)
            .appendingPathComponent(embeddedScreensaverAppexName)
    }

    // MARK: - Screensaver (Idle slot)

    @objc public func isInfinidreamActive() -> Bool {
        // Idle must be the screensaver appex — not the wallpaper Provider.
        if idlePointsAtLegacyModule() {
            return true
        }
        if #available(macOS 14.0, *),
           embeddedScreensaverURL != nil,
           paperSaver.getCurrentScreensaverExtension() == screensaverBundleID {
            return true
        }
        guard embeddedScreensaverURL != nil else {
            return false
        }
        let active = paperSaver.getActiveScreensavers()
        return active.contains(moduleName)
            || active.contains(screensaverBundleID)
    }

    /// Idle: screensaver appex (separate process from Desktop wallpaper).
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

    @objc public func hasActiveScreensaver() -> Bool {
        if paperSaver.activeScreensaver() != nil {
            return true
        }
        return !paperSaver.getActiveScreensavers().isEmpty
            || paperSaver.getCurrentScreensaverExtension() != nil
    }

    @objc public func getActiveScreensaverName() -> String? {
        if #available(macOS 14.0, *),
           let bid = paperSaver.getCurrentScreensaverExtension() {
            return bid
        }
        if let info = paperSaver.activeScreensaver() {
            return info.name
        }
        return paperSaver.getActiveScreensavers().first
    }

    // MARK: - Wallpaper (Desktop slot)

    @objc public func isInfinidreamWallpaperActive() -> Bool {
        guard #available(macOS 14.0, *) else { return false }
        if embeddedWallpaperURL != nil,
           paperSaver.getCurrentWallpaperExtension() == wallpaperBundleID {
            return true
        }
        return desktopPointsAtLegacyModule()
    }

    /// Desktop: wallpaper appex only (does not touch Idle).
    @objc public func setInfinidreamAsWallpaper(completion: @escaping (Bool, Error?) -> Void) {
        Task {
            do {
                if #available(macOS 14.0, *) {
                    if embeddedWallpaperURL != nil {
                        try await setWallpaperExtension(slots: [.desktop])
                    } else {
                        try await writeScreenSaverModuleChoice(slots: [.desktop])
                    }
                    await MainActor.run { completion(true, nil) }
                } else {
                    await MainActor.run {
                        completion(false, Self.error(1, "Live wallpaper requires macOS 14 or later"))
                    }
                }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    /// Desktop via wallpaper appex + Idle via screensaver appex (separate processes).
    @objc public func setInfinidreamAsWallpaperAndScreensaver(
        completion: @escaping (Bool, Error?) -> Void
    ) {
        Task {
            do {
                if #available(macOS 14.0, *), embeddedWallpaperURL != nil {
                    try await setWallpaperExtension(slots: [.desktop])
                } else if #available(macOS 14.0, *) {
                    try await writeScreenSaverModuleChoice(slots: [.desktop])
                }
                try await writeScreenSaverModuleChoice(slots: [.screensaver])
                await MainActor.run { completion(true, nil) }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    // MARK: - Wallpaper extension write (Desktop only)

    /// Activate the wallpaper appex Provider on Desktop and confirm WallpaperAgent
    /// kept the choice. Idle must not be written here.
    private func setWallpaperExtension(slots: Set<WallpaperSlot>) async throws {
        // Defensive: never attach Idle to the wallpaper process.
        if slots.contains(.screensaver) {
            throw Self.error(
                6,
                "Idle must use the screensaver appex, not the wallpaper Provider")
        }
        guard let appex = embeddedWallpaperURL else {
            throw Self.error(3, "Embedded wallpaper appex not found in app PlugIns")
        }
        try await registerWallpaperExtension(appex)

        var config = PaperSaverConfiguration()
        config.validatesWallpaperExtensions = true
        let saver = PaperSaver(configuration: config)

        let listed = (try? await saver.listWallpaperExtensions()) ?? []
        if !listed.contains(where: { $0.bundleIdentifier == wallpaperBundleID }) {
            try await Task.sleep(for: .seconds(1))
            let listed2 = (try? await saver.listWallpaperExtensions()) ?? []
            if !listed2.contains(where: { $0.bundleIdentifier == wallpaperBundleID }) {
                throw Self.error(
                    5,
                    "Wallpaper extension \(wallpaperBundleID) is not registered with pluginkit")
            }
        }

        _ = try await saver.setWallpaperExtension(
            bundleID: wallpaperBundleID,
            slots: slots
        )

        // WallpaperAgent rewrites rejected choices shortly after restart.
        try await Task.sleep(for: .seconds(2))

        if slots.contains(.desktop),
           saver.getCurrentWallpaperExtension() != wallpaperBundleID {
            throw Self.error(
                2,
                "WallpaperAgent did not keep Desktop as \(wallpaperBundleID)")
        }
    }

    private func registerWallpaperExtension(_ url: URL) async throws {
        let ls = Process()
        ls.executableURL = URL(fileURLWithPath:
            "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister")
        ls.arguments = ["-f", url.path]
        try? ls.run()
        ls.waitUntilExit()

        let add = Process()
        add.executableURL = URL(fileURLWithPath: "/usr/bin/pluginkit")
        add.arguments = ["-a", url.path]
        try add.run()
        add.waitUntilExit()

        let elect = Process()
        elect.executableURL = URL(fileURLWithPath: "/usr/bin/pluginkit")
        elect.arguments = ["-e", "use", "-i", wallpaperBundleID]
        try? elect.run()
        elect.waitUntilExit()

        try? await Task.sleep(for: .seconds(1))
    }

    // MARK: - Screensaver module-URL helpers (Idle)

    private func idlePointsAtLegacyModule() -> Bool {
        if let info = paperSaver.activeScreensaver() {
            if info.name == moduleName || info.identifier == embeddedScreensaverAppexName {
                return true
            }
            if info.provider == screensaverBundleID {
                return true
            }
            if let path = info.modulePath, modulePathPointsAtUs(path) {
                return true
            }
        }
        return slotPointsAtOurScreenSaverModule(.screensaver)
    }

    private func desktopPointsAtLegacyModule() -> Bool {
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
        if path.hasSuffix("/\(embeddedScreensaverAppexName)")
            || path.contains("/\(embeddedScreensaverAppexName)/") {
            return true
        }
        if let embedded = embeddedScreensaverURL?.resolvingSymlinksInPath(),
           path == embedded.path {
            return true
        }
        return false
    }

    private func writeScreenSaverModuleChoice(slots: Set<WallpaperSlot>) async throws {
        guard !slots.isEmpty else {
            throw Self.error(4, "No wallpaper slots to write")
        }
        guard let embedded = embeddedScreensaverURL?.resolvingSymlinksInPath() else {
            throw Self.error(3, "Embedded screensaver appex not found in app PlugIns")
        }
        removeLegacyScreenSaversSymlink()

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
        try await Task.sleep(for: .seconds(2))

        var failed: [String] = []
        if slots.contains(.screensaver), !idlePointsAtLegacyModule() {
            failed.append("Idle")
        }
        if slots.contains(.desktop), !desktopPointsAtLegacyModule() {
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
