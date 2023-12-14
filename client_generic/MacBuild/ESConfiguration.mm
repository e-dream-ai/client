#import "ESConfiguration.h"
#import "ESScreensaver.h"
#import "clientversion.h"

#include "EDreamClient.h"
#include "Shepherd.h"

@implementation ESConfiguration

- (IBAction)ok:(id)__unused sender
{
    if (m_checkingLogin)
        return;

    ESScreensaver_InitClientStorage();

    [self saveSettings];

    ESScreensaver_DeinitClientStorage();

    [NSApp endSheet:[self window] returnCode:m_loginWasSuccessful];
}

- (IBAction)cancel:(id)__unused sender
{
    [NSApp endSheet:[self window] returnCode:m_loginWasSuccessful];
}

- (void)awakeFromNib // was - (NSWindow *)window
{
    CFBundleRef bndl = CopyDLBundle_ex();

    NSURL* imgUrl = (NSURL*)CFBridgingRelease(
        CFBundleCopyResourceURL(bndl, CFSTR("red.tif"), NULL, NULL));

    redImage = [[NSImage alloc] initWithContentsOfURL:imgUrl];

    imgUrl = (NSURL*)CFBridgingRelease(
        CFBundleCopyResourceURL(bndl, CFSTR("yellow.tif"), NULL, NULL));

    yellowImage = [[NSImage alloc] initWithContentsOfURL:imgUrl];

    imgUrl = (NSURL*)CFBridgingRelease(
        CFBundleCopyResourceURL(bndl, CFSTR("green.tif"), NULL, NULL));

    greenImage = [[NSImage alloc] initWithContentsOfURL:imgUrl];

    CFRelease(bndl);

    m_checkTimer = nil;

    ESScreensaver_InitClientStorage();

    [self loadSettings];

    ESScreensaver_DeinitClientStorage();

    [drupalPassword setTarget:self];
    [drupalPassword setAction:@selector(doSignIn:)];
}

- (void)fixFlockSize
{
    const char* mp4path =
        [[[contentFldr stringValue] stringByStandardizingPath] UTF8String];

    if (mp4path != NULL && *mp4path)
    {

        size_t flockSize = ESScreensaver_GetFlockSizeMBs(mp4path, 0);
        NSString* str;
        if (flockSize < 1024)
        {
            str = [NSString stringWithFormat:@"It is currently using %ld MB", flockSize];
        }
        else
        {
            str = [NSString stringWithFormat:@"It is currently using %.02f GB", flockSize / 1024.];
        }

        [flockSizeText setStringValue:str];
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
        [loginStatusImage setImage:self->greenImage];
        [loginTestStatusText
            setStringValue:[NSString stringWithFormat:@"Logged in as %@",
                                                      m_origNickname]];
        m_loginWasSuccessful = YES;
        [signInButton setTitle:@"Sign Out"];
        [passwordLabel setHidden:YES];
        [emailLabel setHidden:YES];
        [drupalPassword setTarget:nil];
        [drupalPassword setAction:nil];
        [drupalPassword setHidden:YES];
        [drupalLogin setHidden:YES];
        [loginTestStatusText setFrame:NSMakeRect(158, 56, 204, 18)];
        [loginStatusImage setFrame:NSMakeRect(137, 59, 16, 16)];
        [signInButton setFrame:NSMakeRect(197, 23, 101, 32)];
    }
    else
    {
        [loginStatusImage setImage:self->redImage];
        [loginTestStatusText setStringValue:failMessage];
        m_loginWasSuccessful = NO;
        [signInButton setTitle:@"Sign In"];
        [passwordLabel setHidden:NO];
        [emailLabel setHidden:NO];
        [drupalPassword setTarget:self];
        [drupalPassword setAction:@selector(doSignIn:)];
        [drupalPassword setHidden:NO];
        [drupalLogin setHidden:NO];
        [loginTestStatusText setFrame:NSMakeRect(120, 11, 204, 18)];
        [loginStatusImage setFrame:NSMakeRect(99, 14, 16, 16)];
        [signInButton setFrame:NSMakeRect(338, 5, 101, 32)];
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

    int _statusCode = (int)[httpResponse statusCode];

    if (_statusCode == 200)
    {
    }
    else
    {
        [loginStatusImage setImage:redImage];
        [loginTestStatusText setStringValue:@"Login Failed :("];
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

    [okButton setEnabled:NO];
    [cancelButton setEnabled:NO];
    m_checkingLogin = YES;

    [m_checkTimer invalidate];
    m_checkTimer = nil;

    [loginStatusImage setImage:nil];

    [loginTestStatusText setStringValue:@"Testing Login..."];

    m_httpData = [NSMutableData dataWithCapacity:10];

    NSString* newNickname = [drupalLogin stringValue];
    NSString* newPassword = [drupalPassword stringValue];

    NSString* urlstr;
    NSString* httpMethod;
    NSMutableURLRequest* request =
        [NSMutableURLRequest requestWithURL:[NSURL URLWithString:urlstr]];

    urlstr = @(LOGIN_ENDPOINT);
    httpMethod = @"POST";
    // Set request body data
    NSDictionary* parameters =
        @{@"username" : newNickname, @"password" : newPassword};
    NSError* serializationError;
    NSData* postData =
        [NSJSONSerialization dataWithJSONObject:parameters
                                        options:0
                                          error:&serializationError];
    [request setHTTPBody:postData];

    // Set the HTTP method to POST
    [request setHTTPMethod:httpMethod];
    [request setURL:[NSURL URLWithString:urlstr]];

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
                          [self updateAuthUI:@"Login Failed :("];
                      }
                      self->m_checkingLogin = NO;
                      [self->okButton setEnabled:YES];
                      [self->cancelButton setEnabled:YES];

                      [self->signInButton setEnabled:YES];
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
    [playerFPS setDoubleValue:ESScreensaver_GetDoubleSetting(
                                  "settings.player.player_fps", 23.0)];

    [displayFPS setDoubleValue:ESScreensaver_GetDoubleSetting(
                                   "settings.player.display_fps", 60.0)];

    SInt32 dm = ESScreensaver_GetIntSetting("settings.player.DisplayMode", 2);

    [displayMode selectCellWithTag:dm];

    UInt32 scr =
        (UInt32)abs(ESScreensaver_GetIntSetting("settings.player.screen", 0));

    UInt32 scrcnt = (UInt32)[[NSScreen screens] count];

    if (scr >= scrcnt && scrcnt > 0)
        scr = scrcnt - 1;

    while ([display numberOfItems] > scrcnt)
        [display removeItemAtIndex:scrcnt];

    [display selectItemAtIndex:scr];

    SInt32 mdmode =
        ESScreensaver_GetIntSetting("settings.player.MultiDisplayMode", 0);

    [multiDisplayMode selectItemAtIndex:mdmode];

    [synchronizeVBL setState:ESScreensaver_GetBoolSetting(
                                 "settings.player.vbl_sync", false)];

    [preserveAR setState:ESScreensaver_GetBoolSetting(
                             "settings.player.preserve_AR", false)];

    [blackoutMonitors setState:ESScreensaver_GetBoolSetting(
                                   "settings.player.blackout_monitors", true)];

    [silentMode setState:ESScreensaver_GetBoolSetting(
                             "settings.player.quiet_mode", true)];

#ifdef SCREEN_SAVER
    [blackoutMonitors setHidden:true];
#endif

    [showAttribution setState:ESScreensaver_GetBoolSetting(
                                  "settings.app.attributionpng", true)];

    [useProxy setState:ESScreensaver_GetBoolSetting(
                           "settings.content.use_proxy", false)];

    [proxyHost setStringValue:(__bridge_transfer NSString*)
                                  ESScreensaver_CopyGetStringSetting(
                                      "settings.content.proxy", "")];

    m_origNickname =
        (__bridge_transfer NSString*)ESScreensaver_CopyGetStringSetting(
            "settings.generator.nickname", "");

    //[m_origNickname retain];

    [drupalLogin setStringValue:m_origNickname];

    [drupalPassword setStringValue:@""];

    [proxyLogin setStringValue:(__bridge_transfer NSString*)
                                   ESScreensaver_CopyGetStringSetting(
                                       "settings.content.proxy_username", "")];

    [proxyPassword
        setStringValue:(__bridge_transfer NSString*)
                           ESScreensaver_CopyGetStringSetting(
                               "settings.content.proxy_password", "")];

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

    [cacheSize setIntValue:cache_size];

    [debugLog setState:ESScreensaver_GetBoolSetting("settings.app.log", false)];

    [contentFldr setStringValue:[(__bridge_transfer NSString*)
                                        ESScreensaver_CopyGetStringSetting(
                                            "settings.content.sheepdir", "")
                                    stringByAbbreviatingWithTildeInPath]];

    [version setStringValue:(__bridge NSString*)ESScreensaver_GetVersion()];

    [self fixFlockSize];

    [self updateAuthUI];
}

- (void)saveSettings
{
    double player_fps = [playerFPS doubleValue];

    if (player_fps < .1)
        player_fps = 20.0;

    ESScreensaver_SetDoubleSetting("settings.player.player_fps", player_fps);

    double display_fps = [displayFPS doubleValue];

    if (display_fps < .1)
        display_fps = 60.0;

    ESScreensaver_SetDoubleSetting("settings.player.display_fps", display_fps);

    ESScreensaver_SetIntSetting("settings.player.DisplayMode",
                                (SInt32)[[displayMode selectedCell] tag]);

    ESScreensaver_SetBoolSetting("settings.player.vbl_sync",
                                 [synchronizeVBL state]);

    ESScreensaver_SetBoolSetting("settings.player.preserve_AR",
                                 [preserveAR state]);

    ESScreensaver_SetBoolSetting("settings.player.blackout_monitors",
                                 [blackoutMonitors state]);

    ESScreensaver_SetIntSetting("settings.player.screen",
                                (SInt32)[display indexOfSelectedItem]);

    ESScreensaver_SetIntSetting("settings.player.MultiDisplayMode",
                                (SInt32)[multiDisplayMode indexOfSelectedItem]);

    ESScreensaver_SetBoolSetting("settings.app.attributionpng",
                                 [showAttribution state]);

    ESScreensaver_SetBoolSetting("settings.content.use_proxy",
                                 [useProxy state]);

    ESScreensaver_SetBoolSetting("settings.player.quiet_mode",
                                 [silentMode state]);

    ESScreensaver_SetStringSetting("settings.content.proxy",
                                   [[proxyHost stringValue] UTF8String]);

    ESScreensaver_SetStringSetting(
        "settings.content.sheepdir",
        [[[contentFldr stringValue] stringByStandardizingPath] UTF8String]);

    ESScreensaver_SetStringSetting("settings.generator.nickname",
                                   [[drupalLogin stringValue] UTF8String]);

    ESScreensaver_SetStringSetting("settings.content.proxy_username",
                                   [[proxyLogin stringValue] UTF8String]);

    ESScreensaver_SetStringSetting("settings.content.proxy_password",
                                   [[proxyPassword stringValue] UTF8String]);

    bool unlimited_cache = ([[cacheType selectedCell] tag] == 0);

    ESScreensaver_SetBoolSetting("settings.content.unlimited_cache",
                                 unlimited_cache);

    SInt32 cache_size = [cacheSize intValue];

    ESScreensaver_SetIntSetting("settings.content.cache_size", cache_size);

    ESScreensaver_SetBoolSetting("settings.app.log", [debugLog state]);
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
        NSString* path = [[field stringValue] stringByExpandingTildeInPath];

        [openPanel setCanChooseFiles:NO];
        [openPanel setCanChooseDirectories:YES];
        [openPanel setCanCreateDirectories:YES];
        [openPanel setAllowsMultipleSelection:NO];
        [openPanel
            setDirectoryURL:[NSURL
                                fileURLWithPath:[path stringByStandardizingPath]
                                    isDirectory:YES]];

        result = [openPanel runModal];
        if (result == NSOKButton)
        {
            [field setObjectValue:[[[openPanel directoryURL] path]
                                      stringByAbbreviatingWithTildeInPath]];
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
        [drupalPassword setStringValue:@""];
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
