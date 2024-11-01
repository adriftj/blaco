#include "doctest.h"

#include <ctime>
#include <thread>
#include <chrono>
#include <iostream>
#include <codecvt>
#include <string>
#include <locale>
#include "mcoaio.h"

// convert UTF-8 string to wstring
std::wstring utf8_to_wstring(const std::string& str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.from_bytes(str);
}

// convert wstring to UTF-8 string
std::string wstring_to_utf8(const std::wstring& str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

static const char* s_watchedFiles[] = {
	"watched1.txt",
	"watched2.txt",
};

static void OnWatchedFileChanged(const BlPathChar* fname, int offset, void* parm) {
	time_t t = time(nullptr);
	tm* tm = gmtime(&t);
	const char* sParm = (const char*)parm;
#ifdef WIN32
	std::string sFname = wstring_to_utf8(std::wstring(fname));
	const char* fnameUtf8 = sFname.c_str();
#else
	const char* fnameUtf8 = fname;
#endif
	std::cout << "On watched file [" << fnameUtf8 << "] changed @" << tm->tm_hour
		<< ":" << tm->tm_min << ":" << tm->tm_sec << ",parm=" << sParm << std::endl;
}

const char* s_testData1[] = {
	"First written data\n",
	"Append file data again\n",
	"And append file again and again"
};

static void FileOps(mco_coro* coro) {
	BlFileContentWatcherOpen(".", s_watchedFiles, sizeof(s_watchedFiles) / sizeof(s_watchedFiles[0]),
		2000, OnWatchedFileChanged, (void*)"xxx");
	for (size_t i = 0; i < sizeof(s_testData1) / sizeof(s_testData1[0]); ++i) {
		int f1 = BlFileOpen("./watched1.txt", BLFILE_O_CREAT | BLFILE_O_APPEND, 0666);
		REQUIRE(f1 >= 0);
		int n = strlen(s_testData1[i]);
		REQUIRE(n == McoFileWrite(f1, 0, s_testData1[i], n));
		BlFileClose(f1);
		time_t t = time(nullptr);
		tm* tm = gmtime(&t);
		std::cout << "Changed watched file [" << "./watched1.txt" << "] @" << tm->tm_hour
			<< ":" << tm->tm_min << ":" << tm->tm_sec << std::endl;
		McoSleep(3000);
	}
}

TEST_CASE("est filecontentwatcher") {
	BlInit(0, 0, 0);
	mco_desc desc = mco_desc_init(FileOps, 0);
	mco_coro* coro;
	int r = mco_create(&coro, &desc);
	REQUIRE(r == MCO_SUCCESS);
	McoSchedule(coro, true);
	std::this_thread::sleep_for(std::chrono::seconds(10));
	BlExitNotify();
	BlWaitExited();
}
