# AeroSim - Simulink Integration

The [AeroSim Simulation Platform](https://github.com/aerosim-open/aerosim) integrates with Simulink models through:
- Data input/output S-function blocks that communicate to the simulation through Kafka messages serialized as JSON (based on the [MATLAB Interface *for Apache Kafka*](https://github.com/mathworks-ref-arch/matlab-apache-kafka)).
- A clock synchronization S-function block that provides lock-step co-simulation of the Simulink model with the AeroSim clock steps.
- Executing stand-alone FMU models exported from Simulink through the [Simulink Compiler](https://www.mathworks.com/products/simulink-compiler.html) toolbox's [FMU Builder for Simulink Support Package](https://www.mathworks.com/products/fmubuilder.html)

--- 

To set up the AeroSim Simulink integration, follow these steps:

1. From the aerosim-simulink repo's root folder, run the `init_submodules.sh/bat` script:

    ```sh
    ./init_submodules.sh # init_submodules.bat on Windows
    ```

1. Confirm that the `AEROSIM_SIMULINK_ROOT` environment variable is set to the aerosim-simulink repository root folder.

1. Run the `build.sh/bat` script to build the S-function MEX files into the `aerosim-simulink/aerosim-sfunctions/sfun_mex/` folder:

    ```sh
    ./build.sh # build.bat on Windows
    ```

1. The AeroSim block library with the S-function blocks to use these MEX files is located at `aerosim-simulink/aerosim-sfunctions/aerosim_simulink_block_library.slx`

--- 

To run an example co-simulation demo:

1. Open Matlab and run the `aerosim-simulink/examples/load_aerosim_simulink_cosim_demo.m` example Simulink model.

1. Launch AeroSim with a chosen renderer from the aerosim root repo folder:

    ```
    (from the aerosim repo root folder)
    aerosim/launch_aerosim.sh --unreal
    -or-
    aerosim/launch_aerosim.sh --omniverse
    ```

1. Start the Simulink model. It will wait at the first clock step for AeroSim to connect and start the simulation.

1. Start the example simulation script:

    ```
    (from the aerosim repo root folder)
    source .venv/bin/activate
    cd examples
    python simulink_cosim_and_fmu.py
    ```

When the simulation starts running, the Simulink model steps should start to proceed in lock-step. The Simulink model's slider bar can be used to control the target altitude live, and the Simulink model can be paused and single-stepped.

## License

This project is dual-licensed under both the MIT License and the Apache License, Version 2.0.
You may use this software under the terms of either license, at your option.

See the [LICENSE](LICENSE) file for details.
