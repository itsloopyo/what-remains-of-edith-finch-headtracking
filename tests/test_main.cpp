#include <iostream>

int RunViewInjectionTests();
int RunConfigTests();
int RunDiagnosticsTests();
int RunBuildProfileTests();

int main()
{
    std::cout << "Edith Finch Head Tracking Tests\n";
    std::cout << "===============================\n";

    int failures = 0;
    failures += RunViewInjectionTests();
    failures += RunConfigTests();
    failures += RunDiagnosticsTests();
    failures += RunBuildProfileTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
