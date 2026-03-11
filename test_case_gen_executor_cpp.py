#!/usr/bin/env python3
"""Compatibility wrapper.

This entrypoint now delegates to test_case_gen_executor_cpp_data.py.
"""
from __future__ import annotations

from test_case_gen_executor_cpp_data import main


if __name__ == "__main__":
    main()
