#ifndef HAVE_CONFIG_H
  #error Error! "config.h" required
#endif

#include "config.h"
#include <iostream>
#include <fstream>

#ifdef HAVE_MPI
  #include <mpi.h>
#endif

#include "Oakfoam.h"

void printusage()
{
  std::cout << "Usage: oakfoam [OPTIONS]\n";
  std::cout << "\n";
  std::cout << "Options:\n";
  #ifdef HAVE_WEB
    std::cout << "  --addr IP             bind address for web interface (def: 127.0.0.1)\n";
  #endif
  std::cout << "  -c, --config FILE     execute the GTP commands in FILE first (no output)\n";
  std::cout << "  -h, --help            display this help and exit\n";
  std::cout << "  -l, --log FILE        log everything to FILE\n";
  std::cout << "  --nobook              do not auto load the opening book\n";
  #ifdef HAVE_WEB
    std::cout << "  -p, --port PORT       port for web interface (def: 8000)\n";
  #endif
  std::cout << "  -V, --version         display version and exit\n";
  #ifdef HAVE_WEB
    std::cout << "  --web                 use the web interface\n";
  #endif
  std::cout << "\n";
  std::cout << "Report bugs to: " << PACKAGE_BUGREPORT << "\n";
}

int main(int argc, char* argv[])
{
  #ifdef HAVE_MPI
    MPI::Init(argc,argv);
  #endif
  
  Oakfoam *oakfoam=new Oakfoam();
  
  for(int i=1;i<argc;i++) 
  {
    std::string arg = argv[i];
    if (arg=="-h" || arg=="--help")
    {
      #ifdef HAVE_MPI
        if (MPI::COMM_WORLD.Get_rank()==0)
          printusage();
      #else
        printusage();
      #endif
      delete oakfoam;
      #ifdef HAVE_MPI
        MPI::Finalize();
      #endif
      return 0;
    }
    else if (arg=="-V" || arg=="--version")
    {
      #ifdef HAVE_MPI
        if (MPI::COMM_WORLD.Get_rank()==0)
          std::cout << PACKAGE_STRING << "\n";
      #else
        std::cout << PACKAGE_STRING << "\n";
      #endif
      delete oakfoam;
      #ifdef HAVE_MPI
        MPI::Finalize();
      #endif
      return 0;
    }
    else if (arg=="-c" || arg=="--config")
    {
      i++;
      #ifdef HAVE_MPI
        if (MPI::COMM_WORLD.Get_rank()!=0)
          continue;
      #endif
      
      if (i>=argc)
      {
        std::cerr << "error missing file\n";
        delete oakfoam;
        #ifdef HAVE_MPI
          MPI::Finalize();
        #endif
        return 1;
      }
      std::ifstream fin(argv[i]);
      
      if (!fin)
      {
        std::cerr << "error opening file: " << argv[i] << "\n";
        delete oakfoam;
        #ifdef HAVE_MPI
          MPI::Finalize();
        #endif
        return 1;
      }
      
      oakfoam->gtpe->getOutput()->setOutputOn(false);
      
      std::string line;
      while (std::getline(fin,line))
      {
        if (!oakfoam->gtpe->executeCommand(line))
        {
          fin.close();
          delete oakfoam;
          #ifdef HAVE_MPI
            MPI::Finalize();
          #endif
          return 0;
        }
      }
      
      fin.close();
      
      oakfoam->gtpe->finishLastCommand();
      oakfoam->gtpe->getOutput()->setOutputOn(true);
    }
    else if (arg=="-l" || arg=="--log")
    {
      i++;
      if (i>=argc)
      {
        std::cerr << "error missing file\n";
        delete oakfoam;
        #ifdef HAVE_MPI
          MPI::Finalize();
        #endif
        return 1;
      }
      
      //std::ofstream *logfile=new std::ofstream();
      //logfile->open(argv[i],std::ios::out|std::ios::app);
      FILE *logfile=fopen(argv[i],"a+");
      
      //if (!logfile->is_open())
      if (!logfile)
      {
        std::cerr << "error opening file: " << argv[i] << "\n";
        delete oakfoam;
        #ifdef HAVE_MPI
          MPI::Finalize();
        #endif
        return 1;
      }
      
      oakfoam->gtpe->getOutput()->setLogFile(logfile);
    }
    else if (arg=="--nobook")
    {
      oakfoam->book_autoload=false;
    }
    #ifdef HAVE_WEB
      else if (arg=="--web")
      {
        oakfoam->web_interface=true;
      }
      else if (arg=="-p" || arg=="--port")
      {
        i++;
        if (i>=argc)
        {
          std::cerr << "missing port number\n";
          delete oakfoam;
          #ifdef HAVE_MPI
            MPI::Finalize();
          #endif
          return 1;
        }
        
        std::istringstream iss(argv[i]);
        int port;
        if (iss >> port)
          oakfoam->web_port=port;
        else
        {
          std::cerr << "invalid port number\n";
          delete oakfoam;
          #ifdef HAVE_MPI
            MPI::Finalize();
          #endif
          return 1;
        }
      }
      else if (arg=="--addr")
      {
        i++;
        if (i>=argc)
        {
          std::cerr << "missing bind address\n";
          delete oakfoam;
          #ifdef HAVE_MPI
            MPI::Finalize();
          #endif
          return 1;
        }
        
        oakfoam->web_address=argv[i];
      }
    #endif
    else
    {
      #ifdef HAVE_MPI
        if (MPI::COMM_WORLD.Get_rank()==0)
          std::cout << "Invalid argument: " << arg << "\n";
      #else
        std::cout << "Invalid argument: " << arg << "\n";
      #endif
      delete oakfoam;
      #ifdef HAVE_MPI
        MPI::Finalize();
      #endif
      return 1;
    }
  }
  
  oakfoam->run();
  
  delete oakfoam;
  
  #ifdef HAVE_MPI
    MPI::Finalize();
  #endif
  
  return 0;
}
