teardown () {
  # Echo the output.  Will only be done on a test failure.
  echo "$output"
}

@test "1" {
    skip "The input files are missing"
    run mpirun -n 2 ./test_time_interp_external
    [ "$status" -eq 0 ]
}
