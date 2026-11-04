//
//  ThanksStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import "ThanksStepViewController.h"
#import "ESScreensaver.h"
#ifndef SCREEN_SAVER
#import "ScreensaverInstaller.h"
#endif

@interface ThanksStepViewController ()
@property (weak) IBOutlet NSButton *installAndEnableScreensaver;
@property (weak) IBOutlet NSButton *installAndEnableWallpaper;

@end

@implementation ThanksStepViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do view setup here.
}

- (IBAction)openRemote:(id)sender {
#ifdef STAGE
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://stage.infinidream.ai/rc"]];
#else
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://alpha.infinidream.ai/rc"]];
#endif
}

- (IBAction)openPlaylistBrowser:(id)sender {
#ifdef STAGE
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://stage.infinidream.ai/playlists"]];
#else
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://alpha.infinidream.ai/playlists"]];
#endif
}

- (IBAction)closeModal:(id)sender {
    BOOL installScreensaver = (self.installAndEnableScreensaver.state == NSControlStateValueOn);
    BOOL installWallpaper = (self.installAndEnableWallpaper.state == NSControlStateValueOn);

    ESScreensaver_SetBoolSetting("settings.app.auto_install_screensaver",
                                 installScreensaver || installWallpaper);
    ESScreensaver_SetBoolSetting("settings.app.keep_screensaver_enabled", installScreensaver);
    ESScreensaver_SetBoolSetting("settings.app.keep_wallpaper_enabled", installWallpaper);

#ifndef SCREEN_SAVER
    if (installScreensaver || installWallpaper) {
        [[ScreensaverInstaller sharedInstaller] installScreensaverIfNeeded];
    }
    if (installScreensaver) {
        [[ScreensaverInstaller sharedInstaller] enableScreensaverIfNeeded];
    }
    if (installWallpaper) {
        [[ScreensaverInstaller sharedInstaller] enableWallpaperIfNeeded];
    }
#endif

    [self.view.window.sheetParent endSheet:self.view.window returnCode:NSModalResponseOK];
}

@end
