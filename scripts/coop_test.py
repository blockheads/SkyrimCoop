#!/usr/bin/env python3
"""Command-line entry point for the co-op game harness (docs/design/testing.md)."""
from coop_harness import Session


def main():
    session = Session()
    try:
        session.cli()
    finally:
        session.close()


if __name__ == "__main__":
    main()
