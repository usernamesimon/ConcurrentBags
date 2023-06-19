#!/usr/bin/python3
import ctypes
import os
import datetime


class cBenchResult(ctypes.Structure):
    '''
    This has to match the returned struct in library.c
    '''
    _fields_ = [ ("time", ctypes.c_float),
                  ("num_items", ctypes.c_int),
                   ("num_CASSuc", ctypes.c_int),
                   ("num_CASFail", ctypes.c_int),
                    ("num_Steal", ctypes.c_int) ]

class Benchmark:
    '''
    Class representing a benchmark. It assumes any benchmark sweeps over some
    parameter xrange using the fixed set of inputs for every point. It provides
    two ways of averaging over the given amount of repetitions:
    - represent everything in a boxplot, or
    - average over the results.
    '''
    def __init__(self, bench_function, parameters,
                 repetitions_per_point, xrange, basedir, name):
        self.bench_function = bench_function
        self.parameters = parameters
        self.repetitions_per_point = repetitions_per_point
        self.xrange = xrange
        self.basedir = basedir
        self.name = name

        self.data = {}
        self.now = None

    def run(self):
        '''
        Runs the benchmark with the given parameters. Collects
        repetitions_per_point data points and writes them back to the data
        dictionary to be processed later.
        '''
        self.now = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        print(f"Starting Benchmark run at {self.now}")

        for x in self.xrange:
            tmp = []
            for r in range(0, self.repetitions_per_point):
                result = self.bench_function( x, *self.parameters )
                tmp.append( (result.time*1000,result.num_items,result.num_CASSuc, result.num_CASFail, result.num_Steal) )
            self.data[x] = tmp

    def write_avg_data(self):
        '''
        Writes averages for each point measured into a dataset in the data
        folder timestamped when the run was started.
        '''
        if self.now is None:
            raise Exception("Benchmark was not run. Run before writing data.")

        try:
            os.makedirs(f"{self.basedir}/data/{self.name}")
        except FileExistsError:
            pass
        with open(f"{self.basedir}/data/{self.name}/{self.name}.data", "w")\
                as datafile:
            datafile.write(f"x num_elems avg_time throughput num_CAS_success num_CAS_fails num_Steal\n")
            for x, box in self.data.items():
                times = 0
                Cassuc = 0
                Casfail = 0
                Steal = 0
                for item in box:
                    times += item[0]
                    Cassuc += item[2]
                    Casfail += item[3]
                    Steal += item[4]
                    num_elems = item[1]
                avg_time = times/len(box)
                datafile.write(f"{x} {num_elems} {avg_time} {num_elems*1000/avg_time} {Cassuc} {Casfail} {Steal}\n")

def benchmark():
    '''
    Requires the binary to also be present as a shared library.
    '''
    basedir = os.path.dirname(os.path.abspath(__file__))
    binary = ctypes.CDLL( f"{basedir}/concurrentBagsSimple.so" )
    binary_queue = ctypes.CDLL( f"{basedir}/queue.so" )
    # Set the result type for each benchmark function
    binary.benchmark_add_remove.restype = cBenchResult
    binary.benchmark_random.restype = cBenchResult
    binary.benchmark_half_half.restype = cBenchResult
    binary.benchmark_one_producer.restype = cBenchResult
    binary.benchmark_one_consumer.restype = cBenchResult
    binary_queue.benchmark_random.restype = cBenchResult

    # The number of threads. This is the x-axis in the benchmark, i.e., the
    # parameter that is 'sweeped' over.
    num_threads = [i*2 for i in range(1,5)]#,64]
    num_threads.append(1)
    num_threads = sorted(num_threads)

    # Parameters for the benchmark are passed in a tuple, here (1000,). To pass
    # just one parameter, we cannot write (1000) because that would not parse
    # as a tuple, instead python understands a trailing comma as a tuple with
    # just one entry.
    benchrand_10000 = Benchmark(binary.benchmark_random, (100000,), 20,
                              num_threads, basedir, " benchrand_10000")
    
    bench_add_remove_10000 = Benchmark(binary.benchmark_add_remove, (100000,), 20,
                              num_threads, basedir, " bench_add_remove_10000")
    
    bench_half_half_10000 = Benchmark(binary.benchmark_half_half, (100000,), 20,
                              num_threads, basedir, " bench_half_half_10000")
    
    bench_one_producer_10000 = Benchmark(binary.benchmark_one_producer, (100000,), 20,
                              num_threads, basedir, " bench_one_producer_10000")
    
    bench_one_consumer_10000 = Benchmark(binary.benchmark_one_consumer, (100000,), 20,
                              num_threads, basedir, " bench_one_consumer_10000")

    benchrand_10000_queue = Benchmark(binary_queue.benchmark_random, (100000,), 20,
                               num_threads, basedir, " benchrand_10000_queue")

    benchrand_10000.run()
    benchrand_10000.write_avg_data()
    benchrand_10000_queue.run()
    benchrand_10000_queue.write_avg_data()
    bench_add_remove_10000.run()
    bench_add_remove_10000.write_avg_data()
    bench_half_half_10000.run()
    bench_half_half_10000.write_avg_data()
    bench_one_producer_10000.run()
    bench_one_producer_10000.write_avg_data()
    bench_one_consumer_10000.run()
    bench_one_consumer_10000.write_avg_data()


if __name__ == "__main__":
    benchmark()
