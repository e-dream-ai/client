//
//  main.m
//

#import <Cocoa/Cocoa.h>
#import <Bugsnag/Bugsnag.h>

int main(int argc, char* argv[])
{
    [Bugsnag start];
    return NSApplicationMain(argc, (const char**)argv);
}
