/* uTest
 * Copyright (C) 2011 Data Differential, http://datadifferential.com/
 * Copyright (C) 2006-2009 Brian Aker
 * All rights reserved.
 *
 * Use and distribution licensed under the BSD license.  See
 * the COPYING file in the parent directory for full text.
 */


#include <config.h>

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fnmatch.h>
#include <iostream>

#include "test.h"

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

static void world_stats_print(world_stats_st *stats)
{
   std::cerr << "Total\t\t\t\t" << stats->total << std::endl;
   std::cerr << "\tFailed\t\t\t" << stats->failed << std::endl;
   std::cerr << "\tSkipped\t\t\t" << stats->skipped << std::endl;
   std::cerr << "\tSucceeded\t\t" << stats->success << std::endl;
}

static long int timedif(struct timeval a, struct timeval b)
{
  long us, s;

  us = (long)(a.tv_usec - b.tv_usec);
  us /= 1000;
  s = (long)(a.tv_sec - b.tv_sec);
  s *= 1000;
  return s + us;
}

const char *test_strerror(test_return_t code)
{
  switch (code) {
  case TEST_SUCCESS:
    return "ok";
  case TEST_FAILURE:
    return "failed";
  case TEST_MEMORY_ALLOCATION_FAILURE:
    return "memory allocation";
  case TEST_SKIPPED:
    return "skipped";
  case TEST_MAXIMUM_RETURN:
  default:
     std::cerr << "Unknown return value." << std::endl;
    abort();
  }
}

void create_core(void)
{
  if (getenv("LIBMEMCACHED_NO_COREDUMP") == NULL)
  {
    pid_t pid= fork();

    if (pid == 0)
    {
      abort();
    }
    else
    {
      while (waitpid(pid, NULL, 0) != pid) {};
    }
  }
}


static test_return_t _runner_default(test_callback_fn func, void *p)
{
  if (func)
    return func(p);

  return TEST_SUCCESS;
}

static world_runner_st defualt_runners= {
  _runner_default,
  _runner_default,
  _runner_default
};

static in_port_t global_port= 0;

in_port_t default_port()
{
  assert(global_port);
  return global_port;
}

void set_default_port(in_port_t port)
{
  global_port= port;
}


int main(int argc, char *argv[])
{
  test_return_t return_code;
  unsigned int x;
  char *collection_to_run= NULL;
  char *wildcard= NULL;
  world_st world;
  collection_st *collection;
  collection_st *next;
  void *world_ptr;

  world_stats_st stats;

  memset(&stats, 0, sizeof(stats));
  memset(&world, 0, sizeof(world));
  get_world(&world);

  if (! world.runner)
  {
    world.runner= &defualt_runners;
  }

  collection= world.collections;

  if (world.create)
  {
    test_return_t error;
    world_ptr= world.create(&error);
    if (error != TEST_SUCCESS)
    {
      return EXIT_FAILURE;
    }
  }
  else
  {
    world_ptr= NULL;
  }

  if (argc > 1)
    collection_to_run= argv[1];

  if (argc == 3)
    wildcard= argv[2];

  for (next= collection; next->name; next++)
  {
    test_st *run;

    run= next->tests;
    if (collection_to_run && fnmatch(collection_to_run, next->name, 0))
      continue;

     std::cerr << std::endl << next->name << std::endl << std::endl;

    for (x= 0; run->name; run++)
    {
      struct timeval start_time, end_time;
      long int load_time= 0;

      if (wildcard && fnmatch(wildcard, run->name, 0))
        continue;

       std::cerr << "\tTesting " << run->name;

      if (world.collection_startup)
      {
        world.collection_startup(world_ptr);
      }

      if (run->requires_flush && world.flush)
      {
        world.flush(world_ptr);
      }

      if (world.pre_run)
      {
        world.pre_run(world_ptr);
      }


      if (next->pre && world.runner->pre)
      {
        return_code= world.runner->pre(next->pre, world_ptr);

        if (return_code != TEST_SUCCESS)
        {
          goto error;
        }
      }

      gettimeofday(&start_time, NULL);
      return_code= world.runner->run(run->test_fn, world_ptr);
      gettimeofday(&end_time, NULL);
      load_time= timedif(end_time, start_time);

      if (next->post && world.runner->post)
      {
        (void) world.runner->post(next->post, world_ptr);
      }

      if (world.post_run)
      {
        world.post_run(world_ptr);
      }

error:
      stats.total++;

       std::cerr << "\t\t\t\t\t";

      switch (return_code)
      {
      case TEST_SUCCESS:
         std::cerr << load_time / 1000 << "." << load_time % 1000;
        stats.success++;
        break;
      case TEST_FAILURE:
        stats.failed++;
        break;
      case TEST_SKIPPED:
        stats.skipped++;
        break;
      case TEST_MEMORY_ALLOCATION_FAILURE:
      case TEST_MAXIMUM_RETURN:
      default:
        break;
      }

       std::cerr << "[ " << test_strerror(return_code) << " ]" << std::endl;

      if (world.on_error)
      {
        test_return_t rc;
        rc= world.on_error(return_code, world_ptr);

        if (rc != TEST_SUCCESS)
          break;
      }
    }
  }

   std::cerr << std::endl << std::endl <<  "All tests completed successfully." << std::endl << std::endl;

  if (world.destroy)
  {
    test_return_t error;
    error= world.destroy(world_ptr);

    if (error != TEST_SUCCESS)
    {
       std::cerr << "Failure during shutdown." << std::endl;
      stats.failed++; // We do this to make our exit code return EXIT_FAILURE
    }
  }

  world_stats_print(&stats);

  return stats.failed == 0 ? 0 : 1;
}
