% Add path
addpath(genpath(pwd));

% Load bus object
load evtol_vehicle_bus_object.mat;

% Load scenario
load waypoints_empty.mat;
num_waypoints = length(waypoints);

% Designate start 
init_wypt_num = 1;
init_ned_m = waypoints(1:3,init_wypt_num);
init_rpy_rad = [0; waypoints(5,init_wypt_num); waypoints(4,init_wypt_num)] *pi/180;
max_hover_speed_mps = 15;
vx_slew_rate_mps = 7.5;
vy_slew_rate_mps = 7.5;
vdown_slew_rate_mps = 7.5;

% Export evtol_vehicle_fmu
exportToFMU('evtol_vehicle_fmu', 'FMIVersion', '3.0', 'FMUType', 'CS', ...
            'GenerateLinuxBinaryWithWSL', 'on', ...
            'SaveDirectory', [getenv('AEROSIM_ROOT') '\examples\fmu']);


