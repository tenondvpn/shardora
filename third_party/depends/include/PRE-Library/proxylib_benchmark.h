// benchmark.h
//
// Header for benchmark code

#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define USECS_PER_SECOND 1000000

typedef int BOOL;

// BenchOp class (state for an individual benchmark)
class BenchOp {
 public:
  long mNumTimings;
  long mMaxTimings;
  long *mTimings;
  char mDesc[256];

  BenchOp() { mNumTimings = 0; mTimings = NULL; }
  ~BenchOp();

  void SetDescription(char *description) { strcpy(this->mDesc, description); }
  void SetMaxTimings(long maxTimings) { this->mMaxTimings = maxTimings; 
  this->mTimings = new long[maxTimings]; }
  BOOL AddTiming(long timing) { if (mNumTimings < this->mMaxTimings) { mTimings[mNumTimings++] = timing; return TRUE; } else return FALSE; }
  double Mean();
  double Median() { if (mNumTimings==0) return -1; return mTimings[mNumTimings / 2]; }
  double StdDev() { return -1; }
};

// Benchmark class (a container for many BenchOp classes)
class Benchmark {
 public:
  int mNumops;
  BenchOp *mStats;

  Benchmark(int numops) { this->mNumops = numops; mStats = new BenchOp[numops]; }
  ~Benchmark() { }
  //~Benchmark() { if (this->mStats) { delete this->mStats; } }

  BOOL InitOp(int opnum, int maxTimings, char *description);
  BOOL CollectTiming(int opnum, long microseconds);
  BOOL GetStats(int opnum, double &mean, double &median, double &stddev);
  char *GetDescription(int opnum) { if (opnum >= mNumops) { return NULL; }
  return mStats[opnum].mDesc; }
};

//
// Utility routines
//

void InitBenchmarks(Benchmark &benchmark, long numOps);
long CalculateUsecs(struct timeval &tstart, struct timeval &tend);
ostream& operator<<(ostream& s, const Benchmark &benchmark);

#endif // __BENCHMARK_H__
