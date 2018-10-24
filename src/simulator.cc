/**
 * @file	simulator.cc
 * @author	Nader KHAMMASSI - nader.khammassi@gmail.com 
 * @date 01-01-16
 * @brief      
 */

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

#include <xpu.h>
#include <xpu/runtime>
#include <qx_representation.h>
#include <libqasm_interface.h>
#include <qasm_semantic.hpp>


#include <iostream>
// #include <fstream>

#include "qx_version.h"

// #define assert_error_report_helper(cond) "assertion failed: " #cond
// #ifdef NDEBUG
// #undef assert
// #define assert(cond)  {if(!(cond)) { std::cerr << assert_error_report_helper(cond) "\n"; throw assert_error_report_helper(cond); } }
// #endif


// /* Obtain a backtrace and print it to stdout. */
// void print_trace (void)
// {
//    void *array[10];
//    size_t size;
//    char **strings;
//    size_t i;
//    size = backtrace (array, 10);
//    strings = backtrace_symbols (array, size);

//    std::cout << "Obtained " << size << " stack frames.\n" << std::endl;

//    for (i = 0; i < size; i++)
//       std::cout << strings[i] << std::endl;
//    free (strings);
// }

void print_banner() {
   println("");
   println("  =================================================================================================== "); 
   println("        _______                                                                                       ");
   println("       /  ___   \\   _  __      ____   ____   __  ___  __  __   __    ___  ______  ____    ___         ");
   println("      /  /   /  |  | |/_/     / __/  /  _/  /  |/  / / / / /  / /   / _ |/_  __/ / __ \\  / _ \\        ");
   println("     /  /___/  /  _>  <      _\\ \\   _/ /   / /|_/ / / /_/ /  / /__ / __ | / /   / /_/ / / , _/        ");
   println("     \\______/\\__\\ /_/|_|    /___/  /___/  /_/  /_/  \\____/  /____//_/ |_|/_/    \\____/ /_/|_|         ");
   println("                                                                                                      ");
   println("     version " << QX_VERSION << " - QuTech - " << QX_RELEASE_YEAR << " - report bugs and suggestions to: nader.khammassi@gmail.com");
   println("  =================================================================================================== ");
   println("");
}

/**
 * simulator
 */
int main(int argc, char **argv)
{
   std::string file_path;
   size_t ncpu = 0;
   size_t navg = 0;
   print_banner();
   // print_trace();
   // std::fstream logger_file;
   // std::string log_file_path("./log_where_am_i.log");

   // logger_file.open(log_file_path,
   //                  std::fstream::out);

   if (!(argc == 2 || argc == 3 || argc == 4))
   {
      println("error : you must specify a circuit file !");
      println("usage: \n   " << argv[0] << " file.qc [iterations] [num_cpu]");
      return -1;
   }

   try{
      // parse arguments and initialise xpu cores
      file_path = argv[1];
      if (argc > 2) navg = (atoi(argv[2]));
      if (argc > 3) ncpu = (atoi(argv[3]));
      if (ncpu && ncpu < 128) xpu::init(ncpu);
      else xpu::init();
   }
   catch (...)
   {
      std::cerr << "simulator.cc: init" << std::endl;
      return -1;
   }

   // parse file and create abstract syntax tree
   println("[+] loading circuit from '" << file_path << "' ...");
   FILE * qasm_file = fopen(file_path.c_str(), "r");
   if (!qasm_file)
   {
      std::cerr << "[x] error: could not open " << file_path << std::endl;
      xpu::clean();
      return -1;
   }

   //FIXME:
   // logger_file << "In 110:" << std::endl;
   // logger_file.close();

   // construct libqasm parser and safely parse input file
   compiler::QasmSemanticChecker * parser;
   compiler::QasmRepresentation ast;
   try
   {
      parser = new compiler::QasmSemanticChecker(qasm_file);
      ast = parser->getQasmRepresentation();
   }
   catch (std::exception &e)
   {
      std::cerr << "error while parsing file " << file_path << ": " << std::endl;
      std::cerr << e.what() << std::endl;
      xpu::clean();
      return -1;
   }

   // quantum state and circuits
   size_t                     qubits = ast.numQubits();
   qx::qu_register *          reg = NULL;
   std::vector<qx::circuit*>  circuits;
   std::vector<qx::circuit*>  noisy_circuits;
   std::vector<qx::circuit *> perfect_circuits; 
   // error model parameters
   size_t                     total_errors      = 0;
   double                     error_probability = 0;
   qx::error_model_t          error_model       = qx::__unknown_error_model__;

   // create the quantum state
   println("[+] creating quantum register of " << qubits << " qubits... ");
   // logger_file.open(log_file_path,
   //                  std::fstream::out | std::fstream::app);
   // logger_file << "line 144: " << std::endl;
   // logger_file.close();
   try {
      reg = new qx::qu_register(qubits);
   } catch(std::bad_alloc& exception) {
      std::cerr << "[x] not enough memory, aborting" << std::endl;
      xpu::clean();
      return -1;
   } catch(std::exception& exception) {
      std::cerr << "[x] unexpected exception (" << exception.what() << "), aborting" << std::endl;
      xpu::clean();
      return -1;
   }

   // convert libqasm ast to qx internal representation
   // qx::QxRepresentation qxr = qx::QxRepresentation(qubits);
   std::vector<compiler::SubCircuit> subcircuits = ast.getSubCircuits().getAllSubCircuits();
   __for_in(subcircuit, subcircuits)
   {
      try
      {
         // qxr.circuits().push_back(load_cqasm_code(qubits, *subcircuit));
         perfect_circuits.push_back(load_cqasm_code(qubits, * subcircuit));
      }
      catch (std::string type)
      {
         std::cerr << "[x] encountered unsuported gate: " << type << std::endl;
         xpu::clean();
         return -1;
      }
   }

   println("[i] loaded " << perfect_circuits.size() << " circuits.");
   // logger_file.open(log_file_path,
   //                  std::fstream::out | std::fstream::app);
   // logger_file << "line 179: " << std::endl;
   // logger_file.close();

   // check whether an error model is specified
   if (ast.getErrorModelType() == "depolarizing_channel")
   {
      error_probability = ast.getErrorModelProbability();
      error_model       = qx::__depolarizing_channel__;
   }

   // measurement averaging
   if (navg)
   {
      if (error_model == qx::__depolarizing_channel__)
      {
         // logger_file.open(log_file_path,
         //         std::fstream::out | std::fstream::app);
         // logger_file << "line 188: " << std::endl;
         // logger_file.close();
         try{
            qx::measure m;
            for (size_t s=0; s<navg; ++s)
            {
               reg->reset();
               for (size_t i=0; i<perfect_circuits.size(); i++)
               {
                  if (perfect_circuits[i]->size() == 0)
                     continue;
                  size_t iterations = perfect_circuits[i]->get_iterations();
                  if (iterations > 1)
                  {
                     for (size_t it=0; it<iterations; ++it)
                     {
                        qx::noisy_dep_ch(perfect_circuits[i],error_probability,total_errors)->execute(*reg,false,true);
                     }
                  } else
                        qx::noisy_dep_ch(perfect_circuits[i],error_probability,total_errors)->execute(*reg,false,true);
               }
               m.apply(*reg);
            }
         }
         catch(...)
         {
            std::cerr << "line 222" << std::endl;
         }
      }
      else
      {
         // logger_file.open(log_file_path,
         //                  std::fstream::out | std::fstream::app);
         // logger_file << "line 229: " << std::endl;
         // logger_file.close();
         try{
            qx::measure m;
            for (size_t s=0; s<navg; ++s)
            {
               reg->reset();
               for (size_t i=0; i<perfect_circuits.size(); i++)
                  perfect_circuits[i]->execute(*reg,false,true);
               m.apply(*reg);
            }
         }
         catch(...)
         {
            std::cerr << "line 243" << std::endl;
         }
      }
      
      println("[+] average measurement after " << navg << " shots:");
      // logger_file.open(log_file_path,
      //                  std::fstream::out | std::fstream::app);
      // logger_file << "line 250: " << std::endl;
      // logger_file.close();
      try{
         reg->dump(true);
      }
      catch(...)
      {
         std::cerr << "line 257" << std::endl;
      }
   }
   else
   {
      // logger_file.open(log_file_path,
      //                  std::fstream::out | std::fstream::app);
      // logger_file << "line 264: " << std::endl;
      // logger_file.close();
      try{
         // if (qxr.getErrorModel() == qx::__depolarizing_channel__)
         if (error_model == qx::__depolarizing_channel__)
         {
            // println("[+] generating noisy circuits (p=" << qxr.getErrorProbability() << ")...");
            for (size_t i=0; i<perfect_circuits.size(); i++)
            {
               if (perfect_circuits[i]->size() == 0)
                  continue;
               // println("[>] processing circuit '" << perfect_circuits[i]->id() << "'...");
               size_t iterations = perfect_circuits[i]->get_iterations();
               if (iterations > 1)
               {
                  for (size_t it=0; it<iterations; ++it)
                     circuits.push_back(qx::noisy_dep_ch(perfect_circuits[i],error_probability,total_errors));
               }
               else
               {
                  circuits.push_back(qx::noisy_dep_ch(perfect_circuits[i],error_probability,total_errors));
               }
            }
            // println("[+] total errors injected in all circuits : " << total_errors);
         }
         else 
            circuits = perfect_circuits; // qxr.circuits();
      }
      catch(...)
      {
         std::cerr << "line 294" << std::endl;
      }

      try{
         // logger_file.open(log_file_path,
         //                  std::fstream::out | std::fstream::app);
         // logger_file << "line 300: " << std::endl;
         // logger_file.close();
         for (size_t i=0; i<circuits.size(); i++)
            circuits[i]->execute(*reg);
         // logger_file.open(log_file_path,
         //                  std::fstream::out | std::fstream::app);
         // logger_file << "line 306: " << std::endl;
         // logger_file.close();
      }
      catch(...)
      {
         std::cerr << "line 311" << std::endl;
      }
   }

   // exit(0);
   try{
      // logger_file.open(log_file_path,
      //                  std::fstream::out | std::fstream::app);
      // logger_file << "line 319: " << std::endl;
      // logger_file.close();
      xpu::clean();
   }
   catch(...)
   {
      std::cerr << "line 325" << std::endl;
   }

   return 0;
}
