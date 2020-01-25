#!/bin/bash

TIME_FORMAT="%U %S"

echo "boxing, int_width, clang_opts, sinc_opts, call_conv, n, user_time, system_time, result, err"

for boxing in "always" "never" "smart" ; do
  for bit_width in {1..512} ; do
    for O in "-O0" "-O1" "-O2" "-O3" "-Obest" ; do
      for sincopt in "-O tail-recursive-mod-cons" "" ; do
        for call_conv in "ccc" "fastcc" "coldcc" "'cc 10'" "webkit_jscc" "anyregcc" "preserve_mostcc" "preserve_allcc" "cxx_fast_tlscc" "swiftcc" ; do
          make clean --silent
          BOXING=$boxing INTWIDTH=$bit_width CLANGOPT=$O SINCOPT="$sincopt" CALLCONV="$call_conv" make --silent 2> "make-err.txt"
          if [ $? -eq 0 ]; then
              :
          else
              continue
          fi

          for n in {1..10} ; do
            echo -n > "nqueens-out.txt"
            /bin/time -qf "$boxing, $bit_width, $O, $sincopt, $call_conv, $n, %U, %S" -o "time-out.txt" ./nqueens $n >> "nqueens-out.txt" 2> "nqueens-err.txt"
            < "time-out.txt" tr -d '\n'
            echo -n ", \""
            cat "nqueens-out.txt" | tr -d '\n'
            echo -n "\", \""
            cat "nqueens-err.txt" | tr -d '\n'
            echo "\""
          done
        done
      done
    done
  done
done
