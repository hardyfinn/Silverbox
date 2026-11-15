#include "check.hpp"
#include <cstring>
bool g_render = false;
bool g_verbose = false;
bool g_writeGolden = false;
int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--render")) g_render = true;
		if (!std::strcmp(argv[i], "-v")) g_verbose = true;
		if (!std::strcmp(argv[i], "--write-golden")) g_writeGolden = true;
	}
	for (auto& t : tests()) {
		int before = failures();
		t.fn();
		std::printf("%s %s\n", failures() == before ? "ok  " : "FAIL", t.name);
	}
	std::printf("\n%zu tests, %d failed checks\n", tests().size(), failures());
	return failures() ? 1 : 0;
}
