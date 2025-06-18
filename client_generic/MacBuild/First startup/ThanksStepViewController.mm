//
//  ThanksStepViewController.m
//  infinidream
//
//  Created by Guillaume Louel on 17/06/2025.
//

#import "ThanksStepViewController.h"

@interface ThanksStepViewController ()

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
    [NSApp stopModalWithCode:NSModalResponseOK];
    [self.view.window orderOut:nil];
}

@end
