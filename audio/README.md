# Native PipeWire audio

`Cutefish.Audio` uses the native PipeWire registry, node/device parameters,
streams, and default metadata. WirePlumber owns device policy, automatic
linking, saved defaults, and stream restoration.

Build dependencies are Qt 6, `libpipewire-0.3 >= 0.3.65`, and `libspa-0.2`.
The desktop session needs PipeWire and WirePlumber. Bluetooth audio also
needs the distribution's SPA Bluetooth plugin. No WirePlumber development
library is required.

The `SinkModel`, `SourceModel`, `SinkInputModel`, `SourceOutputModel`, and
`CardModel` expose live objects through the `AudioObject` role. Volume is a
cubic perceptual scale: `PipeWire.NormalVolume` is `1.0`, silence is `0.0`,
and the backend limit is `1.5`. The settings sliders limit normal use to
100%. Changing the overall volume preserves channel balance; channel
volume setters are also available.

Default-device requests write `default.configured.audio.*`; the UI follows
WirePlumber's confirmed `default.audio.*` selection. Stream movement uses
`target.object` metadata with the target's object serial, not a reusable
registry ID. Hardware volume and mute use saved route parameters when the
device exposes them, with node properties used for other audio objects.

All callbacks run through the Qt event loop. The backend clears models on
connection loss and retries once per second. Playback and monitoring are
stopped before the core is disconnected. Feedback and speaker tests use
short generated tones in native streams; no sound theme decoder or external
playback command is needed. Internal feedback and monitoring streams are
excluded from application models.

The old server-module and stream-restore APIs have been removed. Applications
should use the native models and leave session restoration to WirePlumber.

## Tests

Install `pipewire-bin`, `wireplumber >= 0.5`, `dbus-daemon`, Qt Test, Qt Quick
Controls, and the project's FishUI module. Then run from the workspace root:

```sh
cmake -S cutefish-framework -B /tmp/cutefish-audio-build \
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/cutefish-audio-build --target audio-test
ctest --test-dir /tmp/cutefish-audio-build -R audio-native-pipewire --output-on-failure
```

The test starts private D-Bus, PipeWire, and WirePlumber processes. Runtime,
configuration, cache, and state directories are temporary. The policy-only
WirePlumber profile does not enumerate physical audio or Bluetooth devices.
The test uses two virtual outputs, a generated input, and an exported fake
hardware device. Silent streams negotiate the virtual adapters before
testing their software mixers.

Coverage includes volume conversion and bounds, channel balance, external
updates, default selection, hotplug, saved profiles and routes, playback and
capture routing, feedback capture, model consistency, QML controls, multiple
devices in the actual settings page, and service reconnection. Set
`CUTEFISH_AUDIO_SCREENSHOT` to an image path to save the rendered settings
page during the test.

Physical USB/HDMI hotplug, headset jack detection, Bluetooth codec/profile
switching, and audible channel assignments still require testing on the
corresponding hardware. Virtual and protocol tests cannot establish those
hardware-specific results.
