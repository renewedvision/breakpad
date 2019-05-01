#import "ViewController.h"

#import "client/ios/BreakpadController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  // Do any additional setup after loading the view.
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  [super touchesEnded:touches withEvent:event];
  [[BreakpadController sharedInstance] withBreakpadRef:^(BreakpadRef ref) {
    BreakpadGenerateReport(ref, @{});
  }];
}


@end
