#import "Sparkle/Sparkle.h"
#import <Cocoa/Cocoa.h>

@interface ESConfiguration : NSWindowController
{
    // IBOutlet NSMatrix* displayMode;

    IBOutlet NSTextField* playerFPS;
    IBOutlet NSTextField* displayFPS;
    IBOutlet NSPopUpButton* display;
    IBOutlet NSPopUpButton* multiDisplayMode;
    IBOutlet NSButton* synchronizeVBL;
    IBOutlet NSButton* blackoutMonitors;
    IBOutlet NSButton* silentMode;
    IBOutlet NSButton* showAttribution;
    IBOutlet NSButton* preserveAR;

    // login buttons
    IBOutlet NSButton* signInButton;
    __weak IBOutlet NSButton *retryLoginButton;

    // login fields
    IBOutlet NSTextField* emailTextField;
    __weak IBOutlet NSTextField *digitCodeTextField;
    
    
    __weak IBOutlet NSButton *createAccountButton;
    
    // login labels
    IBOutlet NSTextField* emailLabel;
    IBOutlet NSTextField* digitCodeLabel;

    // needs cheking, this definitely changed in modern macOS
    IBOutlet NSButton* useProxy;
    IBOutlet NSTextField* proxyHost;
    IBOutlet NSTextField* proxyLogin;
    IBOutlet NSSecureTextField* proxyPassword;

    //IBOutlet NSMatrix* cacheType;
    //IBOutlet NSFormCell* cacheSize;
    
    
    __weak IBOutlet NSMatrix *cacheTypeMatrix;
    __weak IBOutlet NSFormCell *cacheSizeFormCell;
    
    
    IBOutlet NSTextField* contentFldr;
    IBOutlet NSButton* debugLog;

    IBOutlet NSTextField* membershipText;

    IBOutlet NSTextField* version;

    IBOutlet NSTextField* flockSizeText;

    IBOutlet NSTextField* loginTestStatusText;

    IBOutlet NSImageView* loginStatusImage;


    IBOutlet NSTextField* serverLabel;
    IBOutlet NSTextField* serverField;

    IBOutlet NSButton* okButton;
    IBOutlet NSButton* cancelButton;

    IBOutlet NSTabView* tabView;

    NSString* m_origNickname;
    NSString* m_previousLoginEmail;
    
    NSMutableData* m_httpData;

    NSImage* redImage;
    NSImage* yellowImage;
    NSImage* greenImage;

    NSTimer* m_checkTimer;

    BOOL m_checkingLogin;
    BOOL m_loginWasSuccessful;
    
    BOOL m_sentCode;
}

- (IBAction)ok:(id)sender;
//- (IBAction)cancel:(id)sender;
- (IBAction)goToCreateAccountPage:(id)sender;
- (IBAction)goToLearnMorePage:(id)sender;
- (IBAction)goToHelpPage:(id)sender;
- (IBAction)chooseContentFolder:(id)sender;
- (IBAction)restartLogin:(id)sender;
- (IBAction)validateLogin:(id)sender;


- (void)fixFlockSize;
- (void)awakeFromNib;
- (void)loadSettings;
- (void)saveSettings;

- (void)dealloc;
- (void)showRestartMessageAndRelaunch;
- (void)relaunchApplication;

@end
