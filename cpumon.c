#include <stdint.h>
#include <stdio.h>
#include <string.h>                 // strlen
#include <stdlib.h>                 // malloc

#include <unistd.h>                 // uid_t sleep()
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>                  // open()
#include <sys/stat.h>               // open()
#include <ncurses.h>
#include "src/cpumonlib.h"
#include "src/guilib.h"

int core_count = 0;
bool display_power_config_flag = 0;
bool display_moving_average_flag = 0;

int main (int argc, char **argv)
{   

    init(&core_count);
    
    char *file[20];
    int gpu_freq;

    float *power_per_domain;

    long period_counter = 0;
    long poll_cycle_counter = 0;

    sensor *freq = malloc( sizeof(sensor) + core_count * sizeof(freq->per_core[0]) ); 
    sensor *load = malloc( sizeof(sensor) + core_count * sizeof(load->per_core[0]) ); 
    sensor *temperature = malloc( sizeof(sensor) + core_count * sizeof(temperature->per_core[0]) ); 
    sensor *voltage = malloc( sizeof(sensor) + core_count * sizeof(voltage->per_core[0]) ); 
    struct battery battery;
    struct power my_power;
    

    long long *work_jiffies_before = malloc((core_count) * sizeof(*work_jiffies_before));                  // store for next interval
    long long *total_jiffies_before = malloc((core_count) * sizeof(*total_jiffies_before));

    float freq_his[AVG_WINDOW];
    float load_his[AVG_WINDOW];
    float temp_his[AVG_WINDOW];
    float voltage_his[AVG_WINDOW];
    float power_his[AVG_WINDOW];

    bool running_with_privileges = FALSE;
    if (geteuid() == 0) {
        running_with_privileges = TRUE; 
    } 

    char *cpu_model = identifiy_cpu();
    
    int command;

    while ((command =getopt(argc, argv, "c:hmps")) != -1){
        switch (command) {
            case 'p':
                display_power_config_flag = 1; break;
            case 'a':
                display_moving_average_flag = 1; break;
            case 'h': 
                //printf("\t-a    : calculates a moving average over the last minute\n");
                printf("\t-p    : displays performance and power configurations\n");
			    printf("\t-h    : displays this help\n");
			    exit(EXIT_SUCCESS);
            default:
			    fprintf(stderr,"Unknown option %c\n",command); exit(EXIT_FAILURE);
        }
    }

    init_gui();

  
    while (1) {
        
        command = kbhit();
        switch (command) {
            case 'p':
                display_power_config_flag = display_power_config_flag ^ 1; break;
            default: 
                sleep(POLL_INTERVAL_S);
        }
        
        freq_ghz(freq->per_core, &freq->cpu_avg, core_count);
        cpucore_load(load->per_core, &load->cpu_avg, work_jiffies_before, total_jiffies_before, core_count);
        get_power_battery_w(&battery.power_now);
    
        freq->runtime_avg = runtime_avg(poll_cycle_counter, &freq->cumulative, &freq->cpu_avg);
        load->runtime_avg = runtime_avg(poll_cycle_counter, &load->cumulative, &load->cpu_avg);
        battery.power_runtime_avg = runtime_avg(poll_cycle_counter, &battery.power_cumulative, &battery.power_now);
                                                   
        freq_his[period_counter] = freq->cpu_avg;
        load_his[period_counter] = load->cpu_avg;
        temp_his[period_counter] = temperature->cpu_avg;

        gpu_freq = gpu();
            
        if (running_with_privileges == TRUE) {
            
            temperature_c(temperature->per_core, &temperature->cpu_avg, core_count);
            voltage_v(voltage->per_core, &voltage->cpu_avg, core_count);
            power_per_domain = power_w();
            my_power.pkg_now = power_per_domain[0];
            
            temperature->runtime_avg = runtime_avg(poll_cycle_counter, &temperature->cumulative, &temperature->cpu_avg);
            voltage->runtime_avg = runtime_avg(poll_cycle_counter, &voltage->cumulative, &voltage->cpu_avg);
            my_power.pkg_runtime_avg = runtime_avg(poll_cycle_counter, &my_power.pkg_cumulative, &my_power.pkg_now);

            voltage_his[period_counter] = voltage->cpu_avg;
            power_his[period_counter] = *power_per_domain;
            
            if (period_counter == 1){
                power_his[0] = *power_per_domain;      // over write the first (wrong) power calculation, so that it doesnt affect the avg as much
            }
        }
        

        if (period_counter < AVG_WINDOW/POLL_INTERVAL_S){   // for last minute history
            period_counter++;
        } else {
            period_counter = 0;                    // reset index
        }
        poll_cycle_counter += 1;
        

        // ------------------  output to terminal ------------------------------
        
        clear();

        attron(A_BOLD);
        printw("\n\t\t%s\n\n", cpu_model);
        attroff(A_BOLD);
        
        
        if (running_with_privileges == TRUE) {
            printw("       f/GHz \tC0%%   Temp/°C\tU/V\n");
            printw("-------------------------------------\n");
            for (int core = 0; core < core_count; core++){   
                printw("Core %d \t%.1f\t%.f\t%.f\t%.2f\n", core, freq->per_core[core], load->per_core[core], temperature->per_core[core], voltage->per_core[core]);
            }
            
            printw("\nCPU\t%.2f\t%.2f\t%.1f\t%.2f\t60-s-avg\n", freq->cpu_avg, load->cpu_avg, temperature->cpu_avg, voltage->cpu_avg); 
            printw("CPU\t%.2f\t%.2f\t%.1f\t%.2f\truntime avg\n", freq->runtime_avg, load->runtime_avg, temperature->runtime_avg, voltage->runtime_avg);

                                                                           
            if (display_moving_average_flag == TRUE) {
                moving_average(period_counter, freq_his, load_his, temp_his, voltage_his, power_his);   
            }
            
            printw("\nGPU\t%d MHz\t\t%.2f W\n\n", gpu_freq, power_per_domain[2]);
            draw_power(power_per_domain, my_power.pkg_runtime_avg);

            *file = read_string("/sys/class/power_supply/BAT1/status");
            printw("\n\nBattery power draw = %.2f W (%s)\n", battery.power_now, *file);

            if (print_fanspeed() != 0) {
                printw("Error accessing the embedded controller. Check if ectool is accessible via commandline.\n");
            }

            if (display_power_config_flag == TRUE) {
                power_config();
            } 
            attron(COLOR_PAIR(RED));
            power_limit_msr(core_count);
            attroff(COLOR_PAIR(RED));
        } 

        
        
        // for debugging purposes, in Visual Code debugging works not in root mode
        if (running_with_privileges == FALSE) 
        {       
            printw("To monitor all metrics, pls run as root.\n\n");

            printw("\tf/GHz \tC0%% \n");
            for (int i = 0; i < core_count; i++){   
                printw("Core %d \t%.1f\t%.f\n", i, freq->per_core[i], load->per_core[i]);
            }
            printw("\nCPU\t%.2f\t%.2f\t60-s-avg\n", freq->cpu_avg, load->cpu_avg);
            printw("CPU\t%.2f\t%.2f\truntime avg\n", freq->runtime_avg, load->runtime_avg);
            printw("\nGPU\t%d\n", gpu_freq);


            *file = read_string("/sys/class/power_supply/BAT1/status");
            printw("\n\nBattery power draw = %.2f W (%s)\n", battery.power_now, *file);
            printw("Battery avg power draw = %.2f W\n", battery.power_runtime_avg);
    
            if (display_power_config_flag == TRUE){
                power_config();
            }
            
        }

    }
    return (EXIT_SUCCESS);

}


