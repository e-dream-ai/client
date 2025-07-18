#import "ESConfiguration.h"
#import "ESScreensaver.h"
#import "clientversion.h"
#import "WritingToolsSafeControls.h"

#include "EDreamClient.h"
#include "ServerConfig.h"
#include "PlatformUtils.h"
#include "CacheManager.h"

//using namespace ContentDownloader;

@implementation ESConfiguration

- (IBAction)ok:(id)__unused sender
{
    if (!okButton.isEnabled)
        return;
    if (m_checkingLogin)
        return;

    [self saveSettings];

    [NSApp endSheet:self.window returnCode:m_loginWasSuccessful];
}

// @TODO : deprecated
- (IBAction)cancel:(id)__unused sender
{
    if (!cancelButton.isEnabled)
        return;
    [NSApp endSheet:self.window returnCode:m_loginWasSuccessful];
}

// Helper to show a message box
- (void)showErrorAlert:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Authentication Error"];
    [alert setInformativeText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)awakeFromNib // was - (NSWindow *)window
{
    CFBundleRef bndl = CopyDLBundle_ex();

    // We preload system images, this gives us flat design in modern macOS
    redImage = [NSImage imageNamed:NSImageNameStatusUnavailable];
    yellowImage = [NSImage imageNamed:NSImageNameStatusPartiallyAvailable];
    greenImage = [NSImage imageNamed:NSImageNameStatusAvailable];

    CFRelease(bndl);

    m_checkTimer = nil;

    m_sentCode = false;

    emailTextField.cell.scrollable = NO;
    emailTextField.cell.wraps = NO;
    [emailTextField.cell setUsesSingleLineMode:YES];
    
    // Disable Writing tools (Apple Intelligence) 
    if (@available(macOS 15.2, *)) {
        emailTextField.allowsWritingTools = NO;
        digitCodeTextField.allowsWritingTools = NO;
        proxyHost.allowsWritingTools = NO;
        proxyLogin.allowsWritingTools = NO;
        proxyPassword.allowsWritingTools = NO;
        
        // Also disable for NSFormCell if it supports the property
        if ([cacheSizeFormCell respondsToSelector:@selector(setAllowsWritingTools:)]) {
            [(id)cacheSizeFormCell setAllowsWritingTools:NO];
        }
    }
    
    // Replace text fields with WritingToolsSafe versions to prevent hangs because it looks like
    // macOS doesn't respect the flags above
    emailTextField = [self replaceTextFieldWithSafeVersion:emailTextField];
    digitCodeTextField = [self replaceTextFieldWithSafeVersion:digitCodeTextField];
    proxyHost = [self replaceTextFieldWithSafeVersion:proxyHost];
    proxyLogin = [self replaceTextFieldWithSafeVersion:proxyLogin];
    proxyPassword = [self replaceTextFieldWithSafeVersion:proxyPassword];


    // We initially load the settings here, this avoid flickering of the ui
    [self loadSettings];
}


- (NSTextField *)replaceTextFieldWithSafeVersion:(NSTextField *)originalField {
    if (!originalField) return nil;
    
    // Create a new WritingToolsSafe text field with the same properties
    WritingToolsSafeTextField *safeField = [[WritingToolsSafeTextField alloc] initWithFrame:originalField.frame];
    
    // Copy all the important properties
    safeField.stringValue = originalField.stringValue;
    safeField.placeholderString = originalField.placeholderString;
    safeField.font = originalField.font;
    safeField.textColor = originalField.textColor;
    safeField.backgroundColor = originalField.backgroundColor;
    safeField.bordered = originalField.bordered;
    safeField.bezeled = originalField.bezeled;
    safeField.editable = originalField.editable;
    safeField.selectable = originalField.selectable;
    safeField.enabled = originalField.enabled;
    safeField.hidden = originalField.hidden;
    safeField.alignment = originalField.alignment;
    safeField.tag = originalField.tag;
    safeField.identifier = originalField.identifier;
    safeField.toolTip = originalField.toolTip;
    
    // Copy cell properties
    safeField.cell.scrollable = originalField.cell.scrollable;
    safeField.cell.wraps = originalField.cell.wraps;
    [safeField.cell setUsesSingleLineMode:[originalField.cell usesSingleLineMode]];
    
    // Disable Writing Tools
    if ([safeField respondsToSelector:@selector(setAllowsWritingTools:)]) {
        safeField.allowsWritingTools = NO;
    }
    
    // Copy target/action if it exists
    if (originalField.target && originalField.action) {
        safeField.target = originalField.target;
        safeField.action = originalField.action;
    }
    
    // Replace in the view hierarchy
    NSView *superview = originalField.superview;
    if (superview) {
        [superview replaceSubview:originalField with:safeField];
    }
    
    return safeField;
}

- (void)fixFlockSize
{
    const char* mp4path =
        contentFldr.stringValue.stringByStandardizingPath.UTF8String;

    if (mp4path != NULL && *mp4path)
    {

        size_t flockSize = ESScreensaver_GetFlockSizeMBs(mp4path, 0);
        NSString* str;
        if (flockSize < 1024)
        {
            str = [NSString
                stringWithFormat:@"It is currently using %ld MB", flockSize];
        }
        else
        {
            str = [NSString stringWithFormat:@"It is currently using %.02f GB",
                                             flockSize / 1024.];
        }

        flockSizeText.stringValue = str;
    }
}

- (void)updateAuthUI
{
    [self updateAuthUI:@"Please sign in."];
}

- (void)updateAuthUI:(NSString*)failMessage
{
    if (EDreamClient::IsLoggedIn())
    {
        loginStatusImage.image = self->greenImage;
        loginTestStatusText.stringValue =
            [NSString stringWithFormat:@"Signed in as %@", m_origNickname];
        m_loginWasSuccessful = YES;
        signInButton.title = @"Sign out";
        [signInButton setEnabled:true];

        [retryLoginButton setHidden:true];
        [createAccountButton setHidden:YES];
        [tabView setHidden:NO];

        [emailLabel setHidden:YES];
        [digitCodeLabel setHidden:YES];

        [emailTextField setHidden:YES];
        [digitCodeTextField setHidden:YES];

        [signInButton.superview setNeedsLayout:true];
    }
    else
    {
        if (!m_sentCode) {
            loginStatusImage.image = self->redImage;
            loginTestStatusText.stringValue = failMessage;
            m_loginWasSuccessful = NO;
            
            signInButton.title = @"Send code";
            [signInButton setEnabled:true];

            [retryLoginButton setHidden:false];
            [retryLoginButton setEnabled:false];
            [createAccountButton setHidden:false];

            [tabView setHidden:YES];

            [emailLabel setHidden:NO];
            [digitCodeLabel setHidden:NO];

            [emailTextField setHidden:NO];
            [emailTextField setEnabled:YES];
            [digitCodeTextField setHidden:NO];
            [digitCodeTextField setEnabled:NO];
            
            //[signInButton.superview setNeedsLayout:true];
        } else {
            loginStatusImage.image = self->yellowImage;
            loginTestStatusText.stringValue = @"Check your e-mail for confirmation code";
            m_loginWasSuccessful = NO;
            
            signInButton.title = @"Validate";
            [signInButton setEnabled:true];

            [retryLoginButton setHidden:false];
            [retryLoginButton setEnabled:true];
            [createAccountButton setHidden:false];

            [tabView setHidden:YES];

            [emailLabel setHidden:NO];
            [digitCodeLabel setHidden:NO];

            [emailTextField setHidden:NO];
            [emailTextField setEnabled:NO];
            [digitCodeTextField setHidden:NO];
            [digitCodeTextField setEnabled:YES];
        }
    }
}

- (void)connection:(NSURLConnection*)__unused connection
    didReceiveResponse:(NSURLResponse*)response
{
    // This method is called when the server has determined that it
    // has enough information to create the NSURLResponse.

    // It can be called multiple times, for example in the case of a
    // redirect, so each time we reset the data.

    // receivedData is an instance variable declared elsewhere.
    //[receivedData setLength:0];
    NSHTTPURLResponse* httpResponse = (NSHTTPURLResponse*)response;

    if (![httpResponse isKindOfClass:[NSHTTPURLResponse class]])
    {
#ifdef DEBUG
        NSLog(@"Unknown response type: %@", response);
#endif
        return;
    }

    int _statusCode = (int)httpResponse.statusCode;

    if (_statusCode == 200)
    {
    }
    else
    {
        loginStatusImage.image = redImage;
        loginTestStatusText.stringValue = @"Sign-in Failed :(";
    }
}

- (void)connection:(NSURLConnection*)__unused connection
    didReceiveData:(NSData*)data
{
    // Append the new data to receivedData.
    // receivedData is an instance variable declared elsewhere.
    [m_httpData appendData:data];
}

- (BOOL)connection:(NSURLConnection*)__unused connection
    canAuthenticateAgainstProtectionSpace:(NSURLProtectionSpace*)space
{
    return [space.authenticationMethod
        isEqualToString:NSURLAuthenticationMethodServerTrust];
}

- (void)connection:(NSURLConnection*)__unused connection
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
{

    if ([challenge.protectionSpace.authenticationMethod
            isEqualToString:NSURLAuthenticationMethodServerTrust])
    {
        NSURLCredential* credential = [NSURLCredential
            credentialForTrust:challenge.protectionSpace.serverTrust];
        [challenge.sender useCredential:credential
             forAuthenticationChallenge:challenge];
    }

    [challenge.sender
        continueWithoutCredentialForAuthenticationChallenge:challenge];
}


- (void)loadSettings
{
    playerFPS.doubleValue =
        ESScreensaver_GetDoubleSetting("settings.player.player_fps", 23.0);

    displayFPS.doubleValue =
        ESScreensaver_GetDoubleSetting("settings.player.display_fps", 60.0);

    UInt32 scr =
        (UInt32)abs(ESScreensaver_GetIntSetting("settings.player.screen", 0));

    UInt32 scrcnt = (UInt32)[NSScreen screens].count;

    if (scr >= scrcnt && scrcnt > 0)
        scr = scrcnt - 1;

    while (display.numberOfItems > scrcnt)
        [display removeItemAtIndex:scrcnt];

    [display selectItemAtIndex:scr];

    SInt32 mdmode =
        ESScreensaver_GetIntSetting("settings.player.MultiDisplayMode", 0);

    [multiDisplayMode selectItemAtIndex:mdmode];

    synchronizeVBL.state =
        ESScreensaver_GetBoolSetting("settings.player.vbl_sync", false);

    preserveAR.state =
        ESScreensaver_GetBoolSetting("settings.player.preserve_AR", false);

    blackoutMonitors.state =
        ESScreensaver_GetBoolSetting("settings.player.blackout_monitors", true);

    silentMode.state =
        ESScreensaver_GetBoolSetting("settings.player.quiet_mode", true);

#ifdef SCREEN_SAVER
    [blackoutMonitors setHidden:true];
#endif

    showAttribution.state =
        ESScreensaver_GetBoolSetting("settings.app.attributionpng", false);

    useProxy.state =
        ESScreensaver_GetBoolSetting("settings.content.use_proxy", false);

    proxyHost.stringValue =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.content.proxy", "");

    m_origNickname =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.generator.nickname", "");

    emailTextField.stringValue = m_origNickname;

    digitCodeTextField.stringValue = @"";

    proxyLogin.stringValue =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.content.proxy_username", "");

    proxyPassword.stringValue =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.content.proxy_password", "");

    bool unlimited_cache =
        ESScreensaver_GetBoolSetting("settings.content.unlimited_cache", false);

    SInt32 cache_size =
        ESScreensaver_GetIntSetting("settings.content.cache_size", 10);

    if (cache_size == 0)
    {
        unlimited_cache = true;

        cache_size = 10;
    }

    [cacheTypeMatrix selectCellWithTag:(unlimited_cache ? 0 : 1)];

    cacheSizeFormCell.intValue = cache_size;

    debugLog.state = ESScreensaver_GetBoolSetting("settings.app.log", false);
    serverField.stringValue = (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting("settings.content.server", ServerConfig::DEFAULT_DREAM_SERVER);

    contentFldr.stringValue =
        ((__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
             "settings.content.sheepdir", ""))
            .stringByAbbreviatingWithTildeInPath;

    // Put full version with git + date
    version.stringValue = [NSString stringWithFormat:@"%@ %@ %@",
                           (__bridge NSString*)ESScreensaver_GetVersion(),
                           [NSString stringWithUTF8String:PlatformUtils::GetGitRevision().c_str()],
                           [NSString stringWithUTF8String:PlatformUtils::GetBuildDate().c_str()]];

#ifndef DEBUG
    serverLabel.hidden = YES;
    serverField.hidden = YES;
#endif

    
    [self fixFlockSize];

    [self updateAuthUI];
}

- (void)saveSettings
{
    double player_fps = playerFPS.doubleValue;

    if (player_fps < .1)
        player_fps = 20.0;

    ESScreensaver_SetDoubleSetting("settings.player.player_fps", player_fps);

    double display_fps = displayFPS.doubleValue;

    if (display_fps < .1)
        display_fps = 60.0;

    ESScreensaver_SetDoubleSetting("settings.player.display_fps", display_fps);

    // Hardcode displaymode for now to Cubic as we have removed the setting
    ESScreensaver_SetIntSetting("settings.player.DisplayMode",2);

    ESScreensaver_SetBoolSetting("settings.player.vbl_sync",
                                 synchronizeVBL.state);

    ESScreensaver_SetBoolSetting("settings.player.preserve_AR",
                                 preserveAR.state);

    ESScreensaver_SetBoolSetting("settings.player.blackout_monitors",
                                 blackoutMonitors.state);

    ESScreensaver_SetIntSetting("settings.player.screen",
                                (SInt32)display.indexOfSelectedItem);

    ESScreensaver_SetIntSetting("settings.player.MultiDisplayMode",
                                (SInt32)multiDisplayMode.indexOfSelectedItem);

    ESScreensaver_SetBoolSetting("settings.app.attributionpng",
                                 showAttribution.state);

    ESScreensaver_SetBoolSetting("settings.content.use_proxy", useProxy.state);

    ESScreensaver_SetBoolSetting("settings.player.quiet_mode",
                                 silentMode.state);

    ESScreensaver_SetStringSetting("settings.content.proxy",
                                   proxyHost.stringValue.UTF8String);

    ESScreensaver_SetStringSetting(
        "settings.content.sheepdir",
        contentFldr.stringValue.stringByStandardizingPath.UTF8String);

    ESScreensaver_SetStringSetting("settings.generator.nickname",
                                   emailTextField.stringValue.UTF8String);

    ESScreensaver_SetStringSetting("settings.content.proxy_username",
                                   proxyLogin.stringValue.UTF8String);

    ESScreensaver_SetStringSetting("settings.content.proxy_password",
                                   proxyPassword.stringValue.UTF8String);

    bool unlimited_cache = (cacheTypeMatrix.selectedCell.tag == 0);

    ESScreensaver_SetBoolSetting("settings.content.unlimited_cache",
                                 unlimited_cache);

    SInt32 cache_size = cacheSizeFormCell.intValue;

    ESScreensaver_SetIntSetting("settings.content.cache_size", cache_size);

    if (!unlimited_cache) {
        std::uintmax_t cacheSize = (std::uintmax_t)cache_size * 1024 * 1024 * 1024;

        Cache::CacheManager& cm = Cache::CacheManager::getInstance();
        cm.resizeCache(cacheSize);
    }
    
    
    ESScreensaver_SetBoolSetting("settings.app.log", debugLog.state);

    ESScreensaver_SetStringSetting("settings.content.server", serverField.stringValue.UTF8String);
}

// MARK: Create Account
- (IBAction)goToCreateAccountPage:(id)__unused sender
{
#ifdef STAGE
    NSURL* helpURL = [NSURL URLWithString:@"https://stage.infinidream.ai/account"];
#else
    NSURL* helpURL = [NSURL URLWithString:@"https://alpha.infinidream.ai/account"];
#endif
    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

/*- (IBAction)goToLearnMorePage:(id)__unused sender
{
    NSURL* helpURL = [NSURL URLWithString:@"https://infinidream.ai/learnmore"];

    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}*/

- (IBAction)chooseContentFolder:(id)__unused sender
{
    NSTextField* field = nil;

    field = contentFldr;

    if (field)
    {
        NSOpenPanel* openPanel = [NSOpenPanel openPanel];
        NSInteger result = NSOKButton;
        NSString* path = field.stringValue.stringByExpandingTildeInPath;

        [openPanel setCanChooseFiles:NO];
        [openPanel setCanChooseDirectories:YES];
        [openPanel setCanCreateDirectories:YES];
        [openPanel setAllowsMultipleSelection:NO];
        openPanel.directoryURL =
            [NSURL fileURLWithPath:path.stringByStandardizingPath
                       isDirectory:YES];

        result = [openPanel runModal];
        if (result == NSOKButton)
        {
            field.objectValue =
                openPanel.directoryURL.path.stringByAbbreviatingWithTildeInPath;
        }
    }

    [self fixFlockSize];
}

- (IBAction)restartLogin:(id)__unused sender
{
    // Restart validation
    m_loginWasSuccessful = false;
    m_sentCode = false;
    [self updateAuthUI];
}

// MARK: Validate login
- (IBAction)validateLogin:(id)__unused sender
{
    if (EDreamClient::IsLoggedIn())
    {
        // Store the current email before signing out
        m_previousLoginEmail = [emailTextField.stringValue copy];
        
        EDreamClient::SignOut();

        m_sentCode = false;
        digitCodeTextField.stringValue = @"";
        [self updateAuthUI];
    }
    else
    {
        if (!m_sentCode) {
            // Also trim the email address just in case here, AppKit is unpredictible
             NSString *trimmedEmail = [emailTextField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
 

            // Save email
            ESScreensaver_SetStringSetting("settings.generator.nickname",
                                           trimmedEmail.UTF8String);
            
            // Make sure we save that locally
            m_origNickname = trimmedEmail;
            
            // Update the text field with the trimmed value for consistency
            emailTextField.stringValue = trimmedEmail;

            // Ask for the code to be sent
            auto result = EDreamClient::SendCode();

            if (result.success) {
                m_sentCode = true;
                //loginTestStatusText.stringValue = @(result.message.c_str());
                [self updateAuthUI];
            } else {
                m_sentCode = false;
                loginStatusImage.image = redImage;
                [self showErrorAlert:@(result.message.c_str())];
                //loginTestStatusText.stringValue = @(result.message.c_str());
                // Keep email field enabled and reset the UI state
                [emailTextField setEnabled:YES];
                [digitCodeTextField setEnabled:NO];

                [self updateAuthUI];
            }
        } else {
            // Try validating code
            // Ask for the code to be sent
            if (EDreamClient::ValidateCode(digitCodeTextField.stringValue.UTF8String)) {
                // Login successful
                m_sentCode = false;
                m_loginWasSuccessful = true;
                
                // Check if the email has changed from the previous login
                if (m_previousLoginEmail && ![m_previousLoginEmail isEqualToString:emailTextField.stringValue]) {

                    [self showRestartMessageAndRelaunch];
                }
                
                
                EDreamClient::DidSignIn(); // Let client know we signed in
                
                [self updateAuthUI];
            } else {
                // Code validation failed
                m_loginWasSuccessful = false;
                m_sentCode = false;
                [self updateAuthUI];
            }
        }
    }
}

- (IBAction)goToHelpPage:(id)__unused sender
{
#ifdef STAGE
    NSString* urlStr = [NSString stringWithFormat:@"https://stage.infinidream.ai/help?v=%s", CLIENT_VERSION];
#else
    NSString* urlStr = [NSString stringWithFormat:@"https://alpha.infinidream.ai/help?v=%s", CLIENT_VERSION];
#endif

    NSURL* helpURL = [NSURL URLWithString:urlStr];

    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

- (void)dealloc
{
    [emailTextField setDelegate:nil];
    [digitCodeTextField setDelegate:nil];
}

- (void)showRestartMessageAndRelaunch
{
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Account Change Detected"];
    [alert setInformativeText:@"infinidream will now exit. Please restart the application to take your new settings into account."];
    [alert addButtonWithTitle:@"OK"];
    
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            std::exit(0);
        }
    }];
}

- (void)relaunchApplication
{
    std::exit(0);
    
    NSString *appPath = [NSString stringWithUTF8String:PlatformUtils::GetAppPath().c_str()];
    NSURL *appURL = [NSURL fileURLWithPath:appPath];
    
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    NSError *error = nil;
    
    // Launch the new instance
    [workspace openApplicationAtURL:appURL
                      configuration:[NSWorkspaceOpenConfiguration configuration]
              completionHandler:^(NSRunningApplication * _Nullable app, NSError * _Nullable error) {
                  if (app) {
                      // The new instance was launched successfully, now we can exit the current instance
                      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                          std::exit(0);
                      });
                  } else {
                      // If the launch failed, log an error and don't exit
                      g_Log->Error("Failed to relaunch the application. Error: %s", error.localizedDescription.UTF8String);
                  }
              }];
}

@end
