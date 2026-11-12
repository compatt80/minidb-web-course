"""Wait for openGauss, initialize the demo schema, then start Gunicorn."""

from __future__ import annotations

import os
import subprocess

from web.app import initialize_schema, wait_for_database


def main() -> None:
    wait_for_database(int(os.getenv("DB_WAIT_TIMEOUT", "90")))
    initialize_schema()
    subprocess.run(
        [
            "gunicorn",
            "--bind",
            f"0.0.0.0:{os.getenv('APP_PORT', '8080')}",
            "--workers",
            os.getenv("WEB_WORKERS", "2"),
            "--access-logfile",
            "-",
            "web.app:app",
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
