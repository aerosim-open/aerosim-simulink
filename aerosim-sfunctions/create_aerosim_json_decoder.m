%% Create an AeroSim JSON Decoder subsystem for a specific bus object type
%
%% Inputs:
%   model_name          : model where subsystem will be created
%   bus_object_name     : name of the bus object type
%   sfpLength           : maximum length of JSON string (entered as string)
%   sample_time_sec     : subsystem sample time (entered as string, cannot be -1 or 0)

function create_aerosim_json_decoder(model_name, bus_object_name, sfpLength, sample_time_sec)

%% Configurations
subsystem_name = ['AeroSim JSON Decoder - Bus: ' bus_object_name];
subsystem_path = [model_name '/' subsystem_name '/'];

%% Utility Functions
function bus_object_dict = generate_bus_object_dictionary(bus_object_type, parent_name, bus_object_dict)
% Generate dictionary of bus object leaf nodes from '.' notation to usable variable name
% bus_object_dict{'bus.object.leaf.node'} = {'bus_object_leaf_node' 'bus_object_leaf_node_type'}

    bus_object = evalin('base', bus_object_type);
    for idx = 1:length(bus_object.Elements)
        bus_object_element_name = bus_object.Elements(idx).Name;
        bus_object_element_type = bus_object.Elements(idx).DataType;
        curr_name = strcat(parent_name, '.', bus_object_element_name);
        if ~startsWith(bus_object_element_type, 'Bus:')
            bus_object_dict{curr_name} = {strrep(curr_name, '.', '_') bus_object_element_type};
            continue;
        end

        bus_object_dict = generate_bus_object_dictionary(bus_object_element_name, curr_name, bus_object_dict);
    end
end


%% Generate bus object dictionary for metadata and bus_object_name
bus_object_dict = dictionary();
bus_object_dict = generate_bus_object_dictionary('metadata', 'metadata', bus_object_dict);
bus_object_dict = generate_bus_object_dictionary(bus_object_name, bus_object_name, bus_object_dict);


%% Check for duplicated signal names
% TODO: Exit script if duplicated signal names found


%% Create Subsystem Block
add_block('simulink/Ports & Subsystems/Subsystem', subsystem_path);
lh = get_param([subsystem_path 'In1'], 'LineHandles');
delete_line(lh.Outport(1));
delete_block([subsystem_path 'In1']);
delete_block([subsystem_path 'Out1']);
set_param(subsystem_path, 'Position', [0, 0, 600, 200]);


%% Generate Signals_to_Bus Matlab Function Src
signals_to_bus_block_name = sprintf('%sSignals_to_Bus', subsystem_path);

% Generate Matlab Function source code string
signals_to_bus_matlab_func_src = '';
signals_to_bus_matlab_func_src = append(signals_to_bus_matlab_func_src, sprintf('function [%s, %s] = signals_to_bus(', 'metadata', bus_object_name));
signals = keys(bus_object_dict);
for i=1:length(signals)
    signals_to_bus_matlab_func_src = append(signals_to_bus_matlab_func_src, sprintf('...\n\t%s, ', bus_object_dict{signals{i}}{1}));
end
signals_to_bus_matlab_func_src = signals_to_bus_matlab_func_src(1:end-2);
signals_to_bus_matlab_func_src = append(signals_to_bus_matlab_func_src, sprintf(' )\n\n'));
for i=1:length(signals)
    signals_to_bus_matlab_func_src = append(signals_to_bus_matlab_func_src, sprintf('%s = %s;\n', signals{i}, bus_object_dict{signals{i}}{1}));
end

% Create Matlab Function Block
fprintf(1, 'Creating %s ...\n', signals_to_bus_block_name);
load_system('eml_lib');
libname = sprintf('eml_lib/MATLAB Function');
add_block(libname, signals_to_bus_block_name);
S = sfroot();
B = S.find('Path', signals_to_bus_block_name, '-isa', 'Stateflow.EMChart');
B.Script = sprintf('%s', signals_to_bus_matlab_func_src);
B.Outputs(1).DataType = 'Bus: metadata';
B.Outputs(2).DataType = ['Bus: ' bus_object_name];
for i=1:length(signals)
    B.Inputs(i).DataType =  bus_object_dict{signals{i}}{2};
end

% Set position and size of Signals_to_Bus Matlab Function Block
block_size = length(signals);
block_height_scale = 50;
block_height = block_size * block_height_scale;
block_top = 0;
block_bottom = block_top + block_height;
set_param(signals_to_bus_block_name, 'Position', [0, block_top, 600, block_bottom]);
set_param(signals_to_bus_block_name, 'SystemSampleTime', sample_time_sec);


%% Create JSON Parser S-Function Block
json_parser_block_name = [subsystem_path 'AeroSim JSON Decoder'];

% Add AeroSim JSON Parser library block
add_block('aerosim_simulink_block_library/AeroSim JSON Parser', json_parser_block_name);

% Set sfpFields and sfpFieldTypes
sfpFields_str = '{';
sfpFieldTypes_str = '{';
for i=1:length(signals)
    sfpFields_str = append(sfpFields_str, sprintf('''%s'',', signals{i}));
    sfpFieldTypes_str = append(sfpFieldTypes_str, sprintf('''%s'',', bus_object_dict{signals{i}}{2}));
end
sfpFields_str = sfpFields_str(1:end-1);
sfpFields_str = append(sfpFields_str, sprintf('}'));
sfpFieldTypes_str = sfpFieldTypes_str(1:end-1);
sfpFieldTypes_str = append(sfpFieldTypes_str, sprintf('}'));
set_param(json_parser_block_name, 'sfpDirection', 'Decode', 'sfpLength', sfpLength, 'sfpFields', sfpFields_str, 'sfpFieldTypes', sfpFieldTypes_str);

% Set position and size of AeroSim JSON Decoder
block_size = length(signals);
block_height_scale = 50;
block_height = block_size * block_height_scale;
block_top = 0;
block_bottom = block_top + block_height;
set_param(json_parser_block_name, 'Position', [-1000, block_top, -400, block_bottom]);


%% Connect JSON Parser S-Function Block to Signals_to_Bus Matlab Function Block
% Retrieve block handles
json_parser_handle = get_param(json_parser_block_name, 'PortHandles');
signals_to_bus_handle = get_param(signals_to_bus_block_name, 'PortHandles');

% Connect each signal from json_parser to signal_to_bus
for i=1:length(signals)
    if strcmp(bus_object_dict{signals{i}}{2}, 'string')
        % Special case for string requiring an ASCII_to_String block
        ascii_to_string_blockname = sprintf('%s%s_%d', subsystem_path, 'ASCII to String', i);
        output_port_pos = get_param(json_parser_handle.Outport(i), 'position');
        add_block('simulink/String/ASCII to String', ascii_to_string_blockname);
        set_param(ascii_to_string_blockname, 'Position', [output_port_pos(1)+150, output_port_pos(2)-10, output_port_pos(1)+250, output_port_pos(2)+10])

         % Connect json_parser -> ascii_to_string -> signal_to_bus
        ascii_to_string_handle = get_param(ascii_to_string_blockname, 'PortHandles');
        add_line(subsystem_path, json_parser_handle.Outport(i), ascii_to_string_handle.Inport(1));
        add_line(subsystem_path, ascii_to_string_handle.Outport(1), signals_to_bus_handle.Inport(i));
    else
         % Connect json_parser -> signal_to_bus
        add_line(subsystem_path, json_parser_handle.Outport(i), signals_to_bus_handle.Inport(i));
    end
end


%% Connect Subsystem Level Input and Output Port
% msg_input
msg_input_blockname = [subsystem_path 'json_msg_input'];
add_block('simulink/Sources/In1', msg_input_blockname);
input_port_pos = get_param(json_parser_handle.Inport(1), 'position');
set_param(msg_input_blockname, 'Position', [input_port_pos(1)-200, input_port_pos(2)-7, input_port_pos(1)-170, input_port_pos(2)+7]);
msg_input_handle = get_param(msg_input_blockname, 'PortHandles');
add_line(subsystem_path, msg_input_handle.Outport(1), json_parser_handle.Inport(1));

% msg_len_input
msg_len_input_blockname = [subsystem_path 'json_msg_len_input'];
add_block('simulink/Sources/In1', msg_len_input_blockname);
input_port_pos = get_param(json_parser_handle.Inport(2), 'position');
set_param(msg_len_input_blockname, 'Position', [input_port_pos(1)-200, input_port_pos(2)-7, input_port_pos(1)-170, input_port_pos(2)+7]);
msg_len_input_handle = get_param(msg_len_input_blockname, 'PortHandles');
add_line(subsystem_path, msg_len_input_handle.Outport(1), json_parser_handle.Inport(2));

% metadata bus_output
bus_output_blockname = [subsystem_path 'metadata_output'];
add_block('simulink/Sinks/Out1', bus_output_blockname);
output_port_pos = get_param(signals_to_bus_handle.Outport(1), 'position');
set_param(bus_output_blockname, 'Position', [output_port_pos(1)+200, output_port_pos(2)-7, output_port_pos(1)+230, output_port_pos(2)+7]);
set_param(bus_output_blockname, 'OutDataTypeStr', 'Bus: metadata');
bus_output_handle = get_param(bus_output_blockname, 'PortHandles');
add_line(subsystem_path, signals_to_bus_handle.Outport(1), bus_output_handle.Inport(1));

% bus_object_name bus_output
bus_output_blockname = [subsystem_path bus_object_name '_output'];
add_block('simulink/Sinks/Out1', bus_output_blockname);
output_port_pos = get_param(signals_to_bus_handle.Outport(2), 'position');
set_param(bus_output_blockname, 'Position', [output_port_pos(1)+200, output_port_pos(2)-7, output_port_pos(1)+230, output_port_pos(2)+7]);
set_param(bus_output_blockname, 'OutDataTypeStr', ['Bus: ' bus_object_name]);
bus_output_handle = get_param(bus_output_blockname, 'PortHandles');
add_line(subsystem_path, signals_to_bus_handle.Outport(2), bus_output_handle.Inport(1));


end  % End of function