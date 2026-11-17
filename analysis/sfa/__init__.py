"""SF-000 Analysis: studies the snapshot library away from the instrument.

It fetches what SF-000 sent to the web library, measures each snapshot's sound, works out how the
snapshots relate (alike, a step up or down in energy, what follows what), and sends what it found
back to the web library, where the instrument can read it. It never changes anything it fetched.
"""
VERSION = "0.1.0"
MEASURE_VERSION = 5      # a snapshot measured by an older way of measuring is measured again
