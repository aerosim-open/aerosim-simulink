disp('Setting up and loading "aerosim_simulink_cosim_demo.slx" model...')

% Add paths to MATLAB Kafka reference library files
run('setup_matlab_kafka_paths.m')

% Initialize model variables
altitude_tgt = 0.0;
dt_sec = 0.02;

% Load Vehicle State Bus Object
load bus_object.mat;

% Load Simulink model
disp('Opening "aerosim_simulink_cosim_demo.slx" model...')
open('aerosim_simulink_cosim_demo.slx')

disp('Done.')