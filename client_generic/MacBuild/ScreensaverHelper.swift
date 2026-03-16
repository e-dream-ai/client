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
    
    /// Check if infinidream is the currently active screensaver
    @objc public func isInfinidreamActive() -> Bool {
        let activeScreensavers = paperSaver.getActiveScreensavers()
        return activeScreensavers.count == 1 && activeScreensavers[0] == "infinidream"
    }
    
    /// Set infinidream as the active screensaver
    @objc public func setInfinidreamAsActive(completion: @escaping (Bool, Error?) -> Void) {
        Task {
            do {
                // Use "infinidream" as the module name
                try await paperSaver.setScreensaverEverywhere(module: "infinidream")
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
