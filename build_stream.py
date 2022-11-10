import os
import subprocess
from subprocess import Popen, PIPE

def compileStreambenchmark():
    wsInKB = [8192,32768,65536,131072,262144,524288]
    elemSizeInBytes = 8 # Double confirm this with the code
    numElementSet = [int((kb*1024)/elemSizeInBytes) for kb in wsInKB]
    numElementSet.sort()
    benchClass = 0
    os.system(f'mkdir -p build')
    with open('build/make.log','w') as makeLogF :
        for numElements in numElementSet:
            binaryName=f'stream.{numElements}_{benchClass}.GEM5_RV64'
            print(f'Generating {binaryName}')
            makeProc=subprocess.run([
                                        '/usr/bin/make',
                                        f'STREAM_ARRAY_SIZE={numElements}',
                                        f'BENCH_CLASS={benchClass}',
                                        '-j'
                                    ],stdout=makeLogF,stderr=makeLogF,cwd=os.getcwd())
            if (makeProc.returncode != 0):
                print(f'Error: Cannot build {numElements}')
                break
            else :
                with open(f'build/{binaryName}.S','w') as objdmpF :
                    objRunProc=subprocess.run(['/usr/bin/riscv64-linux-gnu-objdump',
                                               '-d',
                                               f'build/{binaryName}'],stdout=objdmpF,
                                               cwd=os.getcwd())
                    if (objRunProc.returncode != 0) :
                        print(f'Error: Cannot dissassemble {binaryName}')
                        break
                    print(f'Built {numElements}')
    print(' '.join([str(x) for x in numElementSet]))
    print(len(numElementSet))

if __name__=="__main__":
    compileStreambenchmark()