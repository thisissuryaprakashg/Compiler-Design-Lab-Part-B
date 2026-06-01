# Demo Screenshots

## 1. Build Successful

![Build](screenshots/buildsuccessful.png)

## 2. Type Mismatch — WITH Sanitizer (Error Detected)

![Type Mismatch Detected](screenshots/typemismatchwithsanitizererrordetected.png)

## 3. Type Mismatch — WITHOUT Sanitizer (Error NOT Detected)

![Type Mismatch Undetected](screenshots/typemismatchwithoutsanitizererrornotdetected.png)

The same buggy program runs silently with `mpicc` — the bug goes completely undetected.

## 4. Deadlock — WITH Sanitizer (Error Detected)

![Deadlock Detected](screenshots/deadlockwithsanitizererrordetected.png)

## 5. Collective Ordering — WITH Sanitizer (Error Detected)

![Collective Order Detected](screenshots/collectiveorderingwithsanitizererrordetected.png)

## 6. Correct Program — No False Positive

![No False Positive](screenshots/nodeadlocknofalsepositive.png)

A correct MPI program runs cleanly with no false alarms.

## 7. Full Test Suite (./run.sh)

![Run Script](screenshots/runsh.png)

All 21 tests pass — 100% detection rate, 0 false positives.
