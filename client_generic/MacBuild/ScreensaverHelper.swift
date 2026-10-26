//
//  ScreensaverHelper.swift
//  infinidream
//
//  Objective-C bridge for PaperSaver functionality
//

import Foundation
import PaperSaverKit

@objc public class ScreensaverHelper: NSObject {
    private let paperSaver = PaperSaver()
    
    @objc nonisolated(unsafe) public static let shared = ScreensaverHelper()
    
    private override init() {
        super.init()
    }
    
    /// PaperSaver identifies an appex screensaver by the .appex file stem
    /// (PRODUCT_NAME), not CFBundleDisplayName.
    private var moduleName: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage") ? "infinidream-stage" : "infinidream"
    }

    /// Check if infinidream is the currently active screensaver
    @objc public func isInfinidreamActive() -> Bool {
        let activeScreensavers = paperSaver.getActiveScreensavers()
        return activeScreensavers.contains(moduleName)
    }
    
    /// Set infinidream as the active screensaver
    @objc public func setInfinidreamAsActive(completion: @escaping (Bool, Error?) -> Void) {
        let module = moduleName
        Task {
            do {
                try await paperSaver.setScreensaverEverywhere(module: module)
                await MainActor.run {
                    completion(true, nil)
                }
            } catch {
                await MainActor.run {
                    completion(false, error)
                }
            }
        }
    }
    
    /// Check if any screensaver (not just infinidream) is active
    @objc public func hasActiveScreensaver() -> Bool {
        return !paperSaver.getActiveScreensavers().isEmpty
    }

    /// Get the name of the first currently active screensaver
    @objc public func getActiveScreensaverName() -> String? {
        let activeScreensavers = paperSaver.getActiveScreensavers()
        return activeScreensavers.first
    }
}
