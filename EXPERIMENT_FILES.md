# DSTF experiment artifact

This repository snapshot contains the source code, experiment runners, query plan, and cleaned JXT-format Foursquare/Yelp datasets needed for the DSTF experiments.

Included datasets:
- `index_storage_scaling_results/data/foursquare/jxt/N_{100000,200000,300000,400000,500000}.csv`
- `index_storage_scaling_results/data/yelp/jxt/N_{100000,200000,300000,400000,500000}.csv`

Generated build directories, raw logs, result CSVs, virtual environments, and encrypted index outputs are intentionally excluded.


Minimal experiment runners kept in this snapshot:
- `index_storage_scaling_results/scripts/run_index_storage_scaling.py`
- `index_storage_scaling_results/scripts/run_index_then_query_range_clean_490.sh`
- `query_range_scaling_results/scripts/generate_query_range_plan.py`
- `query_range_scaling_results/scripts/query_range_scaling_runner.py`
- `query_range_scaling_results/query_range_scaling_query_plan.csv`

Plotting notebooks/scripts, report generators, cleanup helpers, resume wrappers, and raw result files are intentionally excluded.
