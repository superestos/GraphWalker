
#ifndef DEF_GRAPHCHWALKER_ENGINE
#define DEF_GRAPHCHWALKER_ENGINE

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <omp.h>
#include <vector>
#include <map>
#include <sys/time.h>

#include "api/filename.hpp"
#include "api/io.hpp"
#include "logger/logger.hpp"
#include "metrics/metrics.hpp"
#include "api/pthread_tools.hpp"
#include "walks/randomwalk.hpp"

class graphwalker_engine {
public:     
    std::string base_filename;
    int shardsize;  
    int nshards;  
    int nvertices;      
    size_t blocksize;
    int membudget_mb;
    int exec_threads;
    std::vector<std::pair<vid_t, vid_t> > intervals;
    timeval start;
    
    /* State */
    int exec_interval;
    
    /* Metrics */
    metrics &m;
    WalkManager *walk_manager;
        
    void print_config() {
        logstream(LOG_INFO) << "Engine configuration: " << std::endl;
        logstream(LOG_INFO) << " exec_threads = " << exec_threads << std::endl;
        // logstream(LOG_INFO) << " load_threads = " << load_threads << std::endl;
        logstream(LOG_INFO) << " membudget_mb = " << membudget_mb << std::endl;
        logstream(LOG_INFO) << " blocksize = " << blocksize << std::endl;
        // logstream(LOG_INFO) << " scheduler = " << use_selective_scheduling << std::endl;
    }

    double runtime() {
            timeval end;
            gettimeofday(&end, NULL);
            return end.tv_sec-start.tv_sec+ ((double)(end.tv_usec-start.tv_usec))/1.0E6;
        }
        
public:
        
    /**
     * Initialize GraphChi engine
     * @param base_filename prefix of the graph files
     * @param nshards number of shards
     * @param selective_scheduling if true, uses selective scheduling 
     */
    graphwalker_engine(std::string _base_filename, long long _shardsize, int _nshards, metrics &_m) : base_filename(_base_filename), shardsize(_shardsize), nshards(_nshards), m(_m) {
        // m.start_time("iomgr_init");
        // iomgr = new stripedio(m);
        // m.stop_time("iomgr_init");

        membudget_mb = get_option_int("membudget_mb", 1024);
        exec_threads = get_option_int("execthreads", omp_get_max_threads());
        logstream(LOG_INFO) << "Max available exec_threads = " << exec_threads << std::endl;
        omp_set_num_threads(exec_threads);

        load_vertex_intervals(base_filename, shardsize, intervals);
        nvertices = num_vertices();
        walk_manager = new WalkManager(m,nshards,exec_threads,base_filename);

        _m.set("file", _base_filename);
        _m.set("engine", "default");
        _m.set("nshards", (size_t)nshards);
    }
        
    virtual ~graphwalker_engine() {
    }

    void loadSubGraph(int p, Vertex *&vertices ){
        m.start_time("loadSubGraph");
        std::string invlname = intervalname( base_filename, p );
        int inf = open(invlname.c_str(),O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        if (inf < 0) {
            logstream(LOG_FATAL) << "Could not load :" << invlname << " error: " << strerror(errno) << std::endl;
        }
        assert(inf > 0);
        char * buf;
        size_t sz = readfull(inf, &buf);
        char * bufptr = buf;
        int vcnt = sz / sizeof(int);
        int curvertex = 0;
        int numv = (intervals[p].second-intervals[p].first+1);
        // logstream(LOG_INFO) << "LoadSubGraph data of p : " << p << ", numvertices of p = " << numv << ", num(verts+edges) = " << vcnt << "..." << std::endl;
        vertices = (Vertex*)malloc((numv+1)*sizeof(Vertex));
        while( vcnt > 0 ){
            Vertex v;
            v.vid = curvertex + intervals[p].first;
            int dcnt = *((int*)bufptr);
            bufptr += sizeof(int);
            v.outd = 0;
            if(dcnt > 0) v.outv = (vid_t*)malloc(dcnt*sizeof(vid_t));
            for( int i = 0; i < dcnt; i++ ){
                vid_t to = *((vid_t*)bufptr);
                bufptr += sizeof(vid_t);
                v.outv[v.outd++] = to;
            }
            vertices[curvertex++] = v;
            vcnt -= (v.outd + 1);
        }
        free(buf);
        close(inf);
        m.stop_time("loadSubGraph");
        // logstream(LOG_INFO) << "LoadSubGraph data end." << std::endl;
    }

    void load_vertex_intervals(std::string base_filename, long long shardsize, std::vector<std::pair<vid_t, vid_t> > & intervals, bool allowfail=false) {
        std::string intervalsFilename = filename_intervals(base_filename, shardsize);
        std::ifstream intervalsF(intervalsFilename.c_str());
        
        if (!intervalsF.good()) {
            logstream(LOG_ERROR) << "Could not load intervals-file: " << intervalsFilename << std::endl;
        }
        assert(intervalsF.good());
        
        intervals.clear();
        
        vid_t st=0, en;
        for(int i=0; i < nshards; i++) {
            assert(!intervalsF.eof());
            intervalsF >> en;
            intervals.push_back(std::pair<vid_t,vid_t>(st, en));
            st = en + 1;
        }
        // for(int i=0; i < nshards; i++) {
        for(int i=nshards-1; i < nshards; i++) {
             logstream(LOG_INFO) << "shard: " << intervals[i].first << " - " << intervals[i].second << std::endl;
        }
        intervalsF.close();
    }

    virtual size_t num_vertices() {
        return 1 + intervals[nshards - 1].second;
    }

    void exec_updates(RandomWalk &userprogram, Vertex *&vertices ){ //, VertexDataType* vertex_value){
        // unsigned count = walk_manager->readIntervalWalks(exec_interval);
        m.start_time("exec_updates");
        // logstream(LOG_INFO) << "exec_updates.." << std::endl;
        omp_set_num_threads(exec_threads);
        for(int t = 0; t < exec_threads; t++){
            unsigned count = walk_manager->pwalks[t][exec_interval].size();
            #pragma omp parallel for schedule(static)
                for(unsigned i = 0; i < count; i++ ){
                    // logstream(LOG_INFO) << "exec_interval : " << exec_interval << " , walk : " << i << " --> threads." << omp_get_thread_num() << std::endl;
                    WalkDataType walk = walk_manager->pwalks[t][exec_interval][i];
                    userprogram.updateByWalk(walk, i, exec_interval, vertices, *walk_manager );//, vertex_value);
                }
        }
        m.stop_time("exec_updates");
        // walk_manager->writeIntervalWalks(exec_interval);
    }

    void run(RandomWalk &userprogram, float prob) {
        gettimeofday(&start, NULL);
        // srand((unsigned)time(NULL));
        m.start_time("startWalks");
        userprogram.startWalks(*walk_manager, nvertices, intervals);
        m.stop_time("startWalks");
        //initialnizeVertexData();

        m.start_time("runtime");
        /*loadOnDemand -- Interval loop */
        int numIntervals = 0;
        while( userprogram.hasFinishedWalk(*walk_manager) ){
            Vertex *vertices;
            m.start_time("in_run_interval");
            numIntervals++;
            float cc = ((float)rand())/RAND_MAX;
            if( cc < prob ){
                // logstream(LOG_DEBUG) << "proc < 0.2 --> minstep, choose probability = " << cc << std::endl;
                exec_interval = walk_manager->intervalWithMinStep();
            }else{
                // logstream(LOG_DEBUG) << "proc > 0.2 --> maxwalk, choose probability = " << cc << std::endl;
                exec_interval =walk_manager->intervalWithMaxWalks();
            }
            if(numIntervals%10==1) logstream(LOG_DEBUG) << runtime() << "s : numIntervals: " << numIntervals << " : " << exec_interval << std::endl;
            //walk_manager->printWalksDistribution( exec_interval );
            /*load graph and walks info*/
            loadSubGraph(exec_interval, vertices);
            // unsigned count = walk_manager->readIntervalWalks(exec_interval);
            userprogram.before_exec_interval(exec_interval, intervals[exec_interval].first, intervals[exec_interval].second, *walk_manager);
            exec_updates(userprogram, vertices);
            userprogram.after_exec_interval(exec_interval, intervals[exec_interval].first, intervals[exec_interval].second, *walk_manager);
            // userprogram.after_exec_interval(exec_interval, intervals[exec_interval].first, intervals[exec_interval].second, *walk_manager, vertices);

            m.start_time("free vertices");
            for(unsigned i = 0; i < (intervals[exec_interval].second-intervals[exec_interval].first+1); i++){
                if(vertices[i].outd > 0){
                    // logstream(LOG_DEBUG) << "free vertices : i vertices[i].id outd " << i << " " << vertices[i].vid << " " << vertices[i].outd << " " << vertices[i].outv[0] << std::endl;
                    free(vertices[i].outv);
                    vertices[i].outv = NULL;
                }
            }
            free(vertices);
            vertices = NULL;
            m.stop_time("free vertices");

            m.stop_time("in_run_interval");
        } // For Interval loop
        m.stop_time("runtime");
    }
};

#endif