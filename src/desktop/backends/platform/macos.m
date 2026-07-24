#import <Cocoa/Cocoa.h>

void show_error_box(const char *message)
{
    printf("Error: %s\n", message);
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];

        [alert setMessageText:@"Error"];
        [alert setInformativeText:
            [NSString stringWithUTF8String:message]];

        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSAlertStyleCritical];

        [alert runModal];
    }
}