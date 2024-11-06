#include <thread>

extern "C" {
	size_t BL_gNumCpus = std::thread::hardware_concurrency();
}
