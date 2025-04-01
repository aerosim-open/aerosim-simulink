AEROSIM_SIMULINK_ROOT = getenv('AEROSIM_SIMULINK_ROOT');

% Add paths to MATLAB Kafka reference library files
startup_file = fullfile(AEROSIM_SIMULINK_ROOT, '/matlab-apache-kafka/Software/MATLAB/startup.m');
run(startup_file)

% Add path to AeroSim S-function files
aerosim_sfun_mex_path = fullfile(AEROSIM_SIMULINK_ROOT, '/aerosim-sfunctions/sfun_mex');
addpath(aerosim_sfun_mex_path);
aerosim_block_library_path = fullfile(AEROSIM_SIMULINK_ROOT, '/aerosim-sfunctions');
addpath(aerosim_block_library_path);

if ispc % Windows
    % Add path to rdkafka.dll file to Windows path for this Matlab session
    rdkafka_dll_name = 'rdkafka.dll';
    rdkafka_dll_path = which(rdkafka_dll_name);
    rdkafka_dll_dir = rdkafka_dll_path(1:(length(rdkafka_dll_path)-length(rdkafka_dll_name)));
    setenv('PATH', [getenv('PATH') ';' rdkafka_dll_dir]);
% elseif isunix
end

disp('Added paths for Simulink Kafka S-function blocks.')
