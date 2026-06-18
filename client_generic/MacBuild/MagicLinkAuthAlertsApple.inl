#pragma once
// MagicLinkAuthAlertsApple.inl — include from .mm after Foundation/AppKit and "EDreamClient.h".

#import <AppKit/AppKit.h>

namespace {

static NSString *MagicLink_StrOpt(const std::string& s)
{
    if (s.empty())
        return @"";
    return [NSString stringWithUTF8String:s.c_str()];
}

static NSString *MagicLink_AppendRetryHint(NSString *base, int retryAfterSeconds)
{
    if (retryAfterSeconds < 0)
        return base ?: @"";
    NSMutableString *m = [NSMutableString stringWithString:base ?: @""];
    if (m.length > 0)
        [m appendString:@"\n\n"];
    [m appendFormat:@"Try again in %d seconds.", retryAfterSeconds];
    return m;
}

static bool MagicLink_IsRateLimitedSend(const EDreamClient::SendCodeResult& r)
{
    return r.httpCode == 429 || r.errorCode == "RATE_LIMITED";
}

static bool MagicLink_IsRateLimitedValidate(const EDreamClient::ValidateCodeResult& r)
{
    return r.httpCode == 429 || r.errorCode == "RATE_LIMITED";
}

static void MagicLink_PresentAlert(NSWindow *window, NSString *title, NSString *message)
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title ?: @"";
    alert.informativeText = message ?: @"";
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"OK"];
    if (window) {
        [alert beginSheetModalForWindow:window completionHandler:nil];
    } else {
        [alert runModal];
    }
}

static void MagicLink_ShowSendFailureAlert(NSWindow *window, const EDreamClient::SendCodeResult& result)
{
    if (result.httpCode == 0) {
        MagicLink_PresentAlert(window, @"Authentication Error",
                               MagicLink_StrOpt(result.message).length > 0
                                   ? MagicLink_StrOpt(result.message)
                                   : @"Failed to send verification code.");
        return;
    }
    if (MagicLink_IsRateLimitedSend(result)) {
        NSMutableString *body = [NSMutableString
            stringWithString:@"Too many verification requests. Please wait before requesting another code."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            body = srv;
        }
        NSString *msg = MagicLink_AppendRetryHint(body, result.retryAfterSeconds);
        MagicLink_PresentAlert(window, @"Too Many Requests", msg);
        return;
    }
    if (result.errorCode == "USER_NOT_FOUND") {
        NSMutableString *body = [NSMutableString stringWithString:
            @"We could not find an account for this email. Confirm the address or ask for an invite."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            [body appendString:@"\n\n"];
            [body appendString:srv];
        }
        MagicLink_PresentAlert(window, @"Account Not Found", body);
        return;
    }
    if (result.httpCode >= 400 && result.httpCode < 500) {
        if (result.errorCode == "BAD_REQUEST" || result.errorCode == "UNKNOWN") {
            NSString *msg = MagicLink_StrOpt(result.message);
            MagicLink_PresentAlert(window, @"Unable to send code",
                                   msg.length > 0 ? msg : @"Sign-in failed.");
            return;
        }
        NSMutableString *body = [NSMutableString stringWithString:
            @"We couldn't send a verification email. Make sure your email address is correct, then try Send code again."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            [body appendString:@"\n\n"];
            [body appendString:srv];
        }
        MagicLink_PresentAlert(window, @"Unable to send code", body);
        return;
    }
    if (result.httpCode >= 500) {
        NSMutableString *body = [NSMutableString stringWithString:@"Try again later."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            [body appendString:@" "];
            [body appendString:srv];
        }
        MagicLink_PresentAlert(window, @"Server Error", body);
        return;
    }
    MagicLink_PresentAlert(window, @"Authentication Error",
                           MagicLink_StrOpt(result.message).length > 0
                               ? MagicLink_StrOpt(result.message)
                               : @"Failed to send verification code.");
}

static void MagicLink_ShowValidateFailureAlert(NSWindow *window, const EDreamClient::ValidateCodeResult& result)
{
    if (result.httpCode == 0) {
        NSString *msg = MagicLink_StrOpt(result.message);
        MagicLink_PresentAlert(window, @"Authentication Error",
                               msg.length > 0 ? msg
                                              : @"Backend is temporarily unavailable. Please try again shortly.");
        return;
    }
    if (MagicLink_IsRateLimitedValidate(result)) {
        NSMutableString *body =
            [NSMutableString stringWithString:@"Too many sign-in attempts. Please wait and try again."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            body = srv;
        }
        NSString *msg = MagicLink_AppendRetryHint(body, result.retryAfterSeconds);
        MagicLink_PresentAlert(window, @"Too Many Requests", msg);
        return;
    }
    if (result.errorCode == "USER_NOT_FOUND") {
        NSMutableString *body = [NSMutableString stringWithString:
            @"We could not find an account for this email. Confirm the address or ask for an invite."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            [body appendString:@"\n\n"];
            [body appendString:srv];
        }
        MagicLink_PresentAlert(window, @"Account Not Found", body);
        return;
    }
    if (result.httpCode >= 500) {
        NSMutableString *body = [NSMutableString stringWithString:@"Try again later."];
        NSString *srv = MagicLink_StrOpt(result.message);
        if (srv.length > 0) {
            [body appendString:@" "];
            [body appendString:srv];
        }
        MagicLink_PresentAlert(window, @"Server Error", body);
        return;
    }
    if (result.httpCode >= 400 && result.httpCode < 500) {
        if (result.errorCode == "BAD_REQUEST" || result.errorCode == "UNKNOWN") {
            NSString *msg = MagicLink_StrOpt(result.message);
            MagicLink_PresentAlert(window, @"Sign-In Failed", msg.length > 0 ? msg : @"Sign-in failed.");
            return;
        }
        if (result.errorCode == "CODE_LOCKED_OUT") {
            NSString *msg = MagicLink_StrOpt(result.message);
            if (msg.length == 0) {
                msg = @"This verification code has been locked after too many incorrect attempts. "
                      @"Please request a new code.";
            }
            MagicLink_PresentAlert(window, @"Verification code locked", msg);
            return;
        }
        if (result.errorCode == "INVALID_CODE" || result.errorCode == "CODE_EXPIRED" ||
            result.errorCode.empty()) {
            NSMutableString *body = [NSMutableString stringWithString:
                @"Check for typos and check to be sure you have the most recent code. Try again or start over."];
            NSString *srv = MagicLink_StrOpt(result.message);
            if (srv.length > 0) {
                [body appendString:@"\n\n"];
                [body appendString:srv];
            }
            MagicLink_PresentAlert(window, @"Invalid Code", body);
            return;
        }
        NSString *msg = MagicLink_StrOpt(result.message);
        MagicLink_PresentAlert(window, @"Sign-In Failed", msg.length > 0 ? msg : @"Sign-in failed.");
        return;
    }
    NSString *msg = MagicLink_StrOpt(result.message);
    MagicLink_PresentAlert(window, @"Authentication Error",
                           msg.length > 0 ? msg
                                          : @"Backend is temporarily unavailable. Please try again shortly.");
}

}  // namespace