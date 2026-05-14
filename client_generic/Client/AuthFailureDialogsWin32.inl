#pragma once
// AuthFailureDialogsWin32.inl — include after `AuthDialogContent` and "EDreamClient.h" are in scope.

static std::string AppendRetrySecondsHint(std::string message, int retryAfterSeconds)
{
    if (retryAfterSeconds < 0)
        return message;
    if (!message.empty())
        message += "\n\n";
    message += "Try again in " + std::to_string(retryAfterSeconds) + " seconds.";
    return message;
}

static bool IsMagicLinkRateLimitedHttp(const EDreamClient::SendCodeResult& r)
{
    return r.httpCode == 429 || r.errorCode == "RATE_LIMITED";
}

static bool IsMagicLinkRateLimitedHttp(const EDreamClient::ValidateCodeResult& r)
{
    return r.httpCode == 429 || r.errorCode == "RATE_LIMITED";
}

static AuthDialogContent BuildSendCodeFailureDialog(const EDreamClient::SendCodeResult& result)
{
    if (result.httpCode == 0)
    {
        return {"Authentication Error",
                result.message.empty() ? "Failed to send verification code." : result.message};
    }

    if (IsMagicLinkRateLimitedHttp(result))
    {
        std::string message =
            "Too many verification requests. Please wait before requesting another code.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        message = AppendRetrySecondsHint(std::move(message), result.retryAfterSeconds);
        return {"Too Many Requests", std::move(message)};
    }

    if (result.errorCode == "USER_NOT_FOUND")
    {
        std::string message =
            "We could not find an account for this email. Confirm the address or ask for an invite.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        return {"Account Not Found", std::move(message)};
    }

    const bool isClientErrorHttp = (result.httpCode >= 400 && result.httpCode < 500);
    const bool isServerErrorHttp = (result.httpCode >= 500);

    if (isClientErrorHttp)
    {
        if (result.errorCode == "BAD_REQUEST" || result.errorCode == "UNKNOWN")
        {
            return {"Unable to send code",
                    result.message.empty() ? "Sign-in failed." : result.message};
        }
        std::string message =
            "We couldn't send a verification email. Make sure your email address is correct, then try Send code again.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        return {"Unable to send code", std::move(message)};
    }

    if (isServerErrorHttp)
    {
        std::string message = "Try again later.";
        if (!result.message.empty())
            message += " " + result.message;
        return {"Server Error", std::move(message)};
    }

    return {"Authentication Error",
            result.message.empty() ? "Failed to send verification code." : result.message};
}

static AuthDialogContent BuildValidateFailureDialog(const EDreamClient::ValidateCodeResult& result)
{
    if (result.httpCode == 0)
    {
        return {"Authentication Error",
                result.message.empty()
                    ? "Backend is temporarily unavailable. Please try again shortly."
                    : result.message};
    }

    if (IsMagicLinkRateLimitedHttp(result))
    {
        std::string message = "Too many sign-in attempts. Please wait and try again.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        message = AppendRetrySecondsHint(std::move(message), result.retryAfterSeconds);
        return {"Too Many Requests", std::move(message)};
    }

    if (result.errorCode == "USER_NOT_FOUND")
    {
        std::string message =
            "We could not find an account for this email. Confirm the address or ask for an invite.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        return {"Account Not Found", std::move(message)};
    }

    if (result.httpCode >= 500)
    {
        std::string message = "Try again later.";
        if (!result.message.empty())
            message += " " + result.message;
        return {"Server Error", std::move(message)};
    }

    if (result.httpCode >= 400 && result.httpCode < 500)
    {
        if (result.errorCode == "BAD_REQUEST" || result.errorCode == "UNKNOWN")
        {
            return {"Sign-In Failed",
                    result.message.empty() ? "Sign-in failed." : result.message};
        }
        if (result.errorCode == "INVALID_CODE" || result.errorCode == "CODE_EXPIRED" ||
            result.errorCode.empty())
        {
            std::string message =
                "Check for typos and check to be sure you have the most recent code. Try again or start over.";
            if (!result.message.empty())
                message += "\n\n" + result.message;
            return {"Invalid Code", std::move(message)};
        }
        return {"Sign-In Failed",
                result.message.empty() ? "Sign-in failed." : result.message};
    }

    return {"Authentication Error",
            result.message.empty()
                ? "Backend is temporarily unavailable. Please try again shortly."
                : result.message};
}
