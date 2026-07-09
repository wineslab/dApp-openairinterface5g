#!/usr/bin/env python3
"""CI runner for the Spectrum Sharing dApp.

Instantiates spectrum.SpectrumSharingDApp (PyPI `dapps`) and runs its E3
lifecycle against an rfsim gNB, headless, for a bounded time:
setup -> subscribe RF=1 (Spectrum) + RF=2 (L1-KPM) -> run -> release/stop.

Headless: no energyGui/iqPlotterGui/dashboard (default off via **kwargs), no
classifier, controls disabled (control=False). The dApp writes its [SHM] stats
to /tmp/dapp.log.

Exit 0 on a clean run; non-zero if Setup or the subscriptions are refused.
"""
import argparse
import sys
import threading
import time

from spectrum.spectrum_dapp import SpectrumSharingDApp, compute_fft_size
from spectrum.threshold_detector import StaticThresholdDetector


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--encoding-method", default="asn1", help="asn1 or json")
    ap.add_argument("--link", default="zmq")
    ap.add_argument("--transport", default="tcp")
    ap.add_argument("--timed", type=int, default=20, help="run seconds, then stop")
    ap.add_argument("--num-prbs", type=int, default=106)
    ap.add_argument("--threshold-db", type=float, default=53.0)
    args = ap.parse_args()

    detector = StaticThresholdDetector(
        threshold_db=args.threshold_db,
        fft_size=compute_fft_size(args.num_prbs),
    )
    dapp = SpectrumSharingDApp(
        link=args.link,
        transport=args.transport,
        encoding_method=args.encoding_method,
        detector=detector,
        control=False,
        num_prbs=args.num_prbs,
    )

    response, setup_response = dapp.setup_connection()
    if not response:
        print("[runner] RAN refused Setup", file=sys.stderr)
        return 1
    for rf in setup_response["ranFunctionList"]:
        dapp.check_sm_ids(
            rf["ranFunctionIdentifier"],
            rf["telemetryIdentifierList"],
            rf["controlIdentifierList"],
        )

    time.sleep(1)
    # send_subscription_request() returns True only if the gNB accepted BOTH the
    # L1-KPM (RF=2) and Spectrum (RF=1) subscriptions.
    if not dapp.send_subscription_request():
        dapp.stop()
        print("[runner] gNB did not accept the dApp subscriptions", file=sys.stderr)
        return 1

    # Stop after the bounded window; stop() releases and unwinds the dApp.
    threading.Thread(
        target=lambda: (time.sleep(args.timed), dapp.stop()), daemon=False
    ).start()
    dapp.control_loop()  # blocks until stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
