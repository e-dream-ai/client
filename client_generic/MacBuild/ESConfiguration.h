#import <Cocoa/Cocoa.h>
#import "Sparkle/Sparkle.h"

@interface ESConfiguration : NSWindowController 
{
    IBOutlet NSMatrix* displayMode;
	
    IBOutlet NSTextField* playerFPS;
    IBOutlet NSTextField* displayFPS;
	IBOutlet NSPopUpButton* display;
	IBOutlet NSPopUpButton* multiDisplayMode;
	IBOutlet NSButton* synchronizeVBL;
	IBOutlet NSButton* blackoutMonitors;
    IBOutlet NSButton* silentMode;
	IBOutlet NSButton* showAttribution;
    IBOutlet NSButton* preserveAR;
			
    IBOutlet NSTextField* drupalLogin;
    IBOutlet NSSecureTextField* drupalPassword;
    IBOutlet NSTextField* passwordLabel;
    IBOutlet NSTextField* emailLabel;
    IBOutlet NSButton* useProxy;
    IBOutlet NSTextField* proxyHost;
    IBOutlet NSTextField* proxyLogin;
    IBOutlet NSSecureTextField* proxyPassword;

	IBOutlet NSMatrix* cacheType;
	IBOutlet NSFormCell* cacheSize;
	IBOutlet NSTextField* contentFldr;
	IBOutlet NSButton* debugLog;

	
	IBOutlet NSTextField* membershipText;
		
	IBOutlet NSTextField* version;	
	
	IBOutlet NSTextField* flockSizeText;

	
	IBOutlet NSTextField* loginTestStatusText;
	
	IBOutlet NSImageView* loginStatusImage;
	
	IBOutlet NSButton* signInButton;
    
    IBOutlet NSButton* okButton;
    IBOutlet NSButton* cancelButton;
	
	NSString* m_origNickname;

	NSMutableData *m_httpData;
	
	NSImage* redImage;
	NSImage* yellowImage;
	NSImage* greenImage;

	NSTimer* m_checkTimer;
		
	BOOL m_checkingLogin;
    BOOL m_loginWasSuccessful;
}


- (IBAction)ok:(id)sender;
- (IBAction)cancel:(id)sender;
- (IBAction)goToLearnMorePage:(id)sender;
- (IBAction)goToHelpPage:(id)sender;
- (IBAction)chooseContentFolder:(id)sender;
- (IBAction)doSignIn:(id)sender;

- (void)fixFlockSize;
- (void)awakeFromNib;
- (void)loadSettings;
- (void)saveSettings;

- (void)dealloc;

@end
