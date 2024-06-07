#import "ESConfiguration.h"
#import "ESScreensaver.h"
#import "clientversion.h"

#include "EDreamClient.h"
#include "Shepherd.h"
#include "PlatformUtils.h"

using namespace ContentDownloader;

@implementation ESConfiguration

- (IBAction)ok:(id)__unused sender
{
    if (!okButton.isEnabled)
        return;
    if (m_checkingLogin)
        return;

    ESScreensaver_InitClientStorage();

    [self saveSettings];

    ESScreensaver_DeinitClientStorage();

    [NSApp endSheet:self.window returnCode:m_loginWasSuccessful];
}

// @TODO : deprecated
- (IBAction)cancel:(id)__unused sender
{
    if (!cancelButton.isEnabled)
        return;
    [NSApp endSheet:self.window returnCode:m_loginWasSuccessful];
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

    // We initially load the settings here, this avoid flickering of the ui
    ESScreensaver_InitClientStorage();

    [self loadSettings];

    ESScreensaver_DeinitClientStorage();
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
    [self updateAuthUI:@"Please log in."];
}

- (void)updateAuthUI:(NSString*)failMessage
{
    if (EDreamClient::IsLoggedIn())
    {
        loginStatusImage.image = self->greenImage;
        loginTestStatusText.stringValue =
            [NSString stringWithFormat:@"Logged in as %@", m_origNickname];
        m_loginWasSuccessful = YES;
        signInButton.title = @"Sign Out";
        [signInButton setEnabled:true];

        [tabView setHidden:NO];
        [passwordLabel setHidden:YES];
        [emailLabel setHidden:YES];
        [drupalPassword setHidden:YES];
        [drupalLogin setHidden:YES];

/*        NSLog(@"const %f",drupalLogin.topAnchor.constraintsAffectingLayout.firstObject.constant);
        drupalLogin.topAnchor.constraintsAffectingLayout.firstObject.constant = -10;*/

        [signInButton.superview setNeedsLayout:true];
    }
    else
    {
        loginStatusImage.image = self->redImage;
        loginTestStatusText.stringValue = failMessage;
        m_loginWasSuccessful = NO;
        signInButton.title = @"Sign In";
        [signInButton setEnabled:true];
        [tabView setHidden:YES];
        [passwordLabel setHidden:NO];
        [emailLabel setHidden:NO];

        [drupalPassword setHidden:NO];
        [drupalLogin setHidden:NO];

        [signInButton.superview setNeedsLayout:true];
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
        loginTestStatusText.stringValue = @"Login Failed :(";
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

- (void)startTest:(BOOL)useToken
{
    [signInButton setEnabled:NO];

    //[okButton setEnabled:NO];
    //[cancelButton setEnabled:NO];
    [tabView setHidden:YES];
    m_checkingLogin = YES;

    [m_checkTimer invalidate];
    m_checkTimer = nil;

    [loginStatusImage setImage:nil];

    loginTestStatusText.stringValue = @"Please wait...";
    loginStatusImage.image = self->yellowImage;
    
    m_httpData = [NSMutableData dataWithCapacity:10];

    NSString* newNickname = drupalLogin.stringValue;
    NSString* newPassword = drupalPassword.stringValue;

    NSString* urlstr;
    NSString* httpMethod;
    NSMutableURLRequest* request =
        [NSMutableURLRequest requestWithURL:[NSURL URLWithString:urlstr]];

    urlstr = @(Shepherd::GetEndpoint(ENDPOINT_LOGIN));
    httpMethod = @"POST";
    // Set request body data
    NSDictionary* parameters =
        @{@"username" : newNickname, @"password" : newPassword};
    NSError* serializationError;
    NSData* postData =
        [NSJSONSerialization dataWithJSONObject:parameters
                                        options:0
                                          error:&serializationError];
    request.HTTPBody = postData;

    // Set the HTTP method to POST
    request.HTTPMethod = httpMethod;
    request.URL = [NSURL URLWithString:urlstr];

    // Set request headers
    [request setValue:@"application/json, text/plain, */*"
        forHTTPHeaderField:@"Accept"];
    [request setValue:@"application/json; charset=UTF-8"
        forHTTPHeaderField:@"Content-Type"];

    NSURLSession* session = [NSURLSession sharedSession];

    // Create a data task to send the request
    NSURLSessionDataTask* dataTask = [session
        dataTaskWithRequest:request
          completionHandler:^(NSData* data, NSURLResponse* response,
                              NSError* sessionError) {
              dispatch_async(dispatch_get_main_queue(), ^{
                  if (sessionError)
                  {
                      NSLog(@"Request failed with error: %@", sessionError);
                      self->m_loginWasSuccessful = NO;
                      m_checkingLogin = NO;
                      [self updateAuthUI:@"Login error"];
                  }
                  else
                  {
                      NSError* dictionaryError;

                      // Convert JSON data to an NSDictionary
                      NSDictionary* jsonDictionary = [NSJSONSerialization
                          JSONObjectWithData:data
                                     options:0
                                       error:&dictionaryError];
                      NSString* responseString =
                          [[NSString alloc] initWithData:data
                                                encoding:NSUTF8StringEncoding];
                      NSLog(@"Response: %@", responseString);
                      if (dictionaryError)
                      {
#ifdef DEBUG
                          NSLog(@"Unknown response type: %@", response);
#endif
                          self->m_loginWasSuccessful = NO;
                          m_checkingLogin = NO;

                          [self updateAuthUI:@"Login error"];
                      }
                      else
                      {
                          NSNumber* success =
                              [jsonDictionary valueForKey:@"success"];
                          if (success.boolValue)
                          {
                              NSDictionary* dataEntry =
                                  [jsonDictionary valueForKey:@"data"];
                              NSDictionary* tokenDict =
                                  [dataEntry valueForKey:@"token"];
                              NSString* accessToken =
                                  [tokenDict valueForKey:@"AccessToken"];
                              NSString* refreshToken =
                                  [tokenDict valueForKey:@"RefreshToken"];
                              EDreamClient::DidSignIn(accessToken.UTF8String,
                                                      refreshToken.UTF8String);
                          }
                          [self updateAuthUI:@"Login error"];
                      }
                      self->m_checkingLogin = NO;
                  }
              });
          }];

    // Start the data task
    [dataTask resume];

    /*CFHTTPMessageRef dummyRequest =
            CFHTTPMessageCreateRequest(
                    kCFAllocatorDefault,
                    CFSTR("GET"),
                    (CFURLRef)[request URL],
                    kCFHTTPVersion1_1);

    CFHTTPMessageAddAuthentication(
            dummyRequest,
            nil,
            (CFStringRef)[drupalLogin stringValue],
            (CFStringRef)[drupalPassword stringValue],
            kCFHTTPAuthenticationSchemeBasic,
            FALSE);

    NSString *authorizationString =
            (NSString *)CFHTTPMessageCopyHeaderFieldValue(
                    dummyRequest,
                    CFSTR("Authorization"));

    CFRelease(dummyRequest);

    [request setValue:authorizationString
    forHTTPHeaderField:@"Authorization"];*/

    //[NSURLConnection connectionWithRequest:request delegate:self];
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

    drupalLogin.stringValue = m_origNickname;

    drupalPassword.stringValue = @"";

    proxyLogin.stringValue =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.content.proxy_username", "");

    proxyPassword.stringValue =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.content.proxy_password", "");

    bool unlimited_cache =
        ESScreensaver_GetBoolSetting("settings.content.unlimited_cache", true);

    SInt32 cache_size =
        ESScreensaver_GetIntSetting("settings.content.cache_size", 2000);

    if (cache_size == 0)
    {
        unlimited_cache = true;

        cache_size = 2000;
    }

    [cacheType selectCellWithTag:(unlimited_cache ? 0 : 1)];

    cacheSize.intValue = cache_size;

    debugLog.state = ESScreensaver_GetBoolSetting("settings.app.log", false);
    serverField.stringValue = (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting("settings.content.server", DEFAULT_DREAM_SERVER);

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
                                   drupalLogin.stringValue.UTF8String);

    ESScreensaver_SetStringSetting("settings.content.proxy_username",
                                   proxyLogin.stringValue.UTF8String);

    ESScreensaver_SetStringSetting("settings.content.proxy_password",
                                   proxyPassword.stringValue.UTF8String);

    bool unlimited_cache = (cacheType.selectedCell.tag == 0);

    ESScreensaver_SetBoolSetting("settings.content.unlimited_cache",
                                 unlimited_cache);

    SInt32 cache_size = cacheSize.intValue;

    ESScreensaver_SetIntSetting("settings.content.cache_size", cache_size);

    ESScreensaver_SetBoolSetting("settings.app.log", debugLog.state);

    ESScreensaver_SetStringSetting("settings.content.server", serverField.stringValue.UTF8String);
}

- (IBAction)goToCreateAccountPage:(id)__unused sender
{
    NSURL* helpURL = [NSURL URLWithString:@"https://e-dream.ai/register"];

    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

- (IBAction)goToLearnMorePage:(id)__unused sender
{
    NSURL* helpURL = [NSURL URLWithString:@"https://e-dream.ai/learnmore"];

    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

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

- (IBAction)doSignIn:(id)__unused sender
{
    ESScreensaver_InitClientStorage();
    if (EDreamClient::IsLoggedIn())
    {
        EDreamClient::SignOut();
        drupalPassword.stringValue = @"";
        [self updateAuthUI];
    }
    else
    {
        [self startTest:NO];
    }
}

- (IBAction)goToHelpPage:(id)__unused sender
{
    NSString* urlStr = [NSString
        stringWithFormat:@"https://e-dream.ai/help?v=%s", CLIENT_VERSION];

    NSURL* helpURL = [NSURL URLWithString:urlStr];

    [[NSWorkspace sharedWorkspace] openURL:helpURL];
}

- (void)dealloc
{
    [drupalLogin setDelegate:nil];
    [drupalPassword setDelegate:nil];
}

@end
