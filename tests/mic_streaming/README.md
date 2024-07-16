# Test Microphone Pipeline

Audio files from the wake-word-benchmark are played on this machine while the microphone signal is received via UDP from the satellite and recorded.

### Setup

1. install benchmark files

    ```sh
    tests/mic_streaming/setup_testdata.sh
    ```
2. if not already done, install build environment
    ```sh
    source scripts/setup_build_env.sh
    ```

3. if not already done, activate virtual env
    ```sh
    source .venv/bin/activate
    ```

4. install additional requirements:
    ```sh
    pip install -r tests/mic_streaming/requirements.txt
    ```

5. compile & upload firmware (this will set the udp server ip-address to this machine)
    ```sh
    esphome compile config/test_stream_mics.yaml
    esphome upload config/test_stream_mics.yaml
    ```

### Run Test

1. if not already done, activate virtual env
    ```sh
    source .venv/bin/activate
    ```

2. enable `Stream Mic via UDP` switch in HA

3. run test:
    ```
    python tests/mic_streaming/run_test.py
    ```

4. disable `Stream Mic via UDP` switch in HA


### Run Live Streaming
The received mic data is played directly played on this machine and recorded in 10s chunks.
Make sure to use headphones in order to prevent audio feedback.

1. if not already done, activate virtual env
    ```sh
    source .venv/bin/activate
    ```

2. run test:
    ```
    python tests/mic_streaming/run_live_streaming.py
    ```

3. enable `Stream Mic via UDP` switch in HA


4. disable `Stream Mic via UDP` switch in HA




### Recordings
Recordings can be found here:
```
testdata/mic_streaming/
```