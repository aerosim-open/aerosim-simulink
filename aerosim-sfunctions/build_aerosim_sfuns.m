function build_aerosim_sfuns
    % build_aerosim_sfuns Helper function to build AeroSim Kafka S-functions
    % Modifed from MathWorks kafka_build_sfuns.m, Copyright 2019 The MathWorks, Inc.

    this_file_path = fileparts(mfilename('fullpath'));
    aerosim_sfun_src_path = strcat(this_file_path, '/src');
    aerosim_sfun_mex_path = strcat(this_file_path, '/sfun_mex');

    aerosim_kafka_utils_src = strcat(aerosim_sfun_src_path, '/', 'aerosim_kafka_utils.c');
    aerosim_clock_sfun_src = strcat(aerosim_sfun_src_path, '/', 'sl_aerosim_clock_sync.c');
    aerosim_producer_sfun_src = strcat(aerosim_sfun_src_path, '/', 'sl_aerosim_kafka_producer.c');
    aerosim_consumer_sfun_src = strcat(aerosim_sfun_src_path, '/', 'sl_aerosim_kafka_consumer.c');
    aerosim_decode_json_sfun_src = strcat(aerosim_sfun_src_path, '/', 'sf_aerosim_json_parser.cpp');

    % Dependency folders
    jDir = kafka.getRoot('..' ,'CPP', 'jansson');

    % S-function paths
    here = kafka.getRoot('app', 'sfun');

    srcDir = fullfile(here, 'src');
    incDir = fullfile(here, 'inc');
    old = cd(srcDir);
    goBack = onCleanup(@() cd(old));

    % Arguments
    common_args = { ...
        ...'-v', ...
        '-g', ...
        ['-I', incDir], ...
        ['-I', srcDir], ...
        '-lfixedpoint', ...
        '-outdir', aerosim_sfun_mex_path };
    kafkaArgs = kafka.utils.getMexLibArgs();
    if ismac
        error('OSX support not yet added to this package.\n');
    elseif isunix
        jansson = {...
            ['-I"', fullfile(jDir, 'src'), '"'], ...
            ...['-L"', fullfile(jDir, 'src','.libs'), '"'], ...
            '-ljansson',...
            };
        platformArgs = {'-lz'};
    elseif ispc
%         is64 = strcmp('PCWIN64', computer);
%         if is64
%             jDir = getenv('PROGRAMFILES');
%         else
%             jDir = getenv('ProgramFiles(x86)');
%         end
        jansson = { ...
            ['-I"', fullfile(jDir, 'src'), '"'], ...
            ['-I"', fullfile(jDir, 'build.win64', 'include' ), '"'], ...
            ['-L"', fullfile(jDir, 'lib'), '"'], ...
            '-ljansson.lib'};
        platformArgs = {};
    else
        error('Unknown platform\n');
    end

    sfuns = { ...
        {aerosim_clock_sfun_src, aerosim_kafka_utils_src, 'mw_kafka_utils.c', 'mx_kafka_utils.c', jansson{:}}, ...
        {aerosim_producer_sfun_src, 'mw_kafka_utils.c', 'mx_kafka_utils.c'}, ...
        {aerosim_consumer_sfun_src, aerosim_kafka_utils_src, 'mw_kafka_utils.c', 'mx_kafka_utils.c'}, ...
        {aerosim_decode_json_sfun_src, jansson{:}} ...
        }; %#ok<CCAT>

    for k=1:length(sfuns)
        sfun = sfuns{k};
        if iscell(sfun)
            fprintf('Compiling %s ...\n', sfun{1});
            mex(common_args{:}, kafkaArgs{:}, platformArgs{:}, sfun{:});
        else
            fprintf('Compiling %s ...\n', sfun);
            mex(common_args{:}, kafkaArgs{:}, platformArgs{:}, sfun);
        end
    end

end
