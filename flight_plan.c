
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define NO_OF_GROUPS 2
#define NO_OF_STATES 2
#define NO_OF_READERS 1

#define SIG_P2C 1
#define SPI_PATH "/dev/spidev0.1"
int spi_id;
int flag = 0;

uint16_t out_sensor[3];
uint16_t out_kalman[3];
int duty_cycle;

int fd;
// define placeholder functions
void kalmanfilterR(uint16_t *buf, uint16_t *buf2) { return; }
void kalmanfilterS(uint16_t *buf, uint16_t *buf2) { return; }
int pid(uint16_t *buf2) { return 0; }
int pwm(int duty_cycle) { return 0; }

// make struct for each state
typedef struct {
        pid_t pid;
        struct row *head;
        int length;
} state;
// make struct for each row for each state
struct row {
        uint8_t bitmask1[NO_OF_GROUPS];
        uint8_t bitmask0[NO_OF_GROUPS];
        state *next_state;
};
typedef struct row row;
uint8_t input_mask[2] = {0x00, 0x00};
uint8_t clear_mask[2] = {0xFF, 0xF0};
row stat_rows[2];
row rot_rows[2];
state stationary_state = {0, stat_rows, 1};
state rotate_state = {0, rot_rows, 1};
state *current_state;
state *next_state;

// add a row to the state table for a particular state
int add_row(row *head, uint8_t bitmask1, uint8_t bitmask0, int group_no, int row_no,
            state *next_state) {
        static int index;
        head[row_no].bitmask1[group_no] = bitmask1;
        head[row_no].bitmask0[group_no] = bitmask0;
        head[row_no].next_state = next_state;
        return 0;
}
// create the state table
int init_modes() {
        add_row(stat_rows, 0xFF, 0x01, 0, 0, &rotate_state);
        add_row(stat_rows, 0xFF, 0x00, 1, 0, &rotate_state);
        // maybe add row that leads to the current state itself?
        add_row(rot_rows, 0xFF, 0x01, 0, 0, &stationary_state);
        add_row(rot_rows, 0xFF, 0x00, 1, 0, &stationary_state);
        return 0;
}

int compare_input_mask(uint8_t inputmask[], row row) {
        for (int i = 0; i < NO_OF_GROUPS; i++) {
                if (~((inputmask[i] & row.bitmask1[i]) | (~inputmask[i] & ~row.bitmask0[i]))) {
                        return 0;
                }
        }
        return 1;
}

// if A: input
//    B: bitmask1
//    C: bitmask0
//    then (AB + A'C')' = 0 will give the next state to go to
void sigstophandler(int signo, siginfo_t *info, void *context) {
        if (signo == SIGTSTP) {
                        flag = 1;
        }
}
void sigusrhandler(int signo, siginfo_t *info, void *context) {
        uint32_t sigv = info->si_value.sival_int;
        uint8_t address1 = (sigv & 0xFF000000) >> 24;
        uint8_t address0 = (sigv & 0x00FF0000) >> 16;
        uint8_t bitmask1_1 = (address1 % 2) ? ((sigv & 0x0000F000) >> 8) | 0x0F
                                            : ((sigv & 0x0000F000) >> 12) | 0xF0;
        uint8_t bitmask0_1 = (sigv & 0x00000F00) >> 8; // bitmask0 of group 1
        uint8_t bitmask1_0 = (address0 % 2) ? (sigv & 0x000000F0) | 0x0F
                                            : ((sigv & 0x000000F0) >> 4) | 0xF0;
        uint8_t bitmask0_0 = sigv & 0x0000000F;
        input_mask[address1 / 2] = (input_mask[address1 / 2] & bitmask1_1) |
                                   (input_mask[address1 / 2] & bitmask0_1) |
                                   (bitmask1_1 & bitmask0_1);
        input_mask[address0 / 2] = (input_mask[address0 / 2] & bitmask1_0) |
                                   (input_mask[address0 / 2] & bitmask0_0) |
                                   (bitmask1_0 & bitmask0_0);
        for (int i = 0; i < current_state->length; i++) {
                if (compare_input_mask(input_mask, current_state->head[i])) {
                        next_state = current_state->head[i].next_state;
                        break;
                }
        }
        if (next_state == current_state)
                return;
        kill(current_state->pid, SIGTSTP);
        int status;
        if (waitpid(current_state->pid, &status, WUNTRACED) < 0) {
                perror("waitpid");
                exit(1);
        }
        if (WIFSTOPPED(status)) {
                kill(next_state->pid, SIGCONT);
                if (waitpid(next_state->pid, &status, WCONTINUED) < 0) {
                        perror("waitpid");
                        exit(1);
                }
                if (WIFCONTINUED(status)) {
                        current_state = next_state;
                }
        }
        for (int i = 0; i < NO_OF_GROUPS; i++) {
                input_mask[i] = input_mask[i] & clear_mask[i];
        }
}
int main() {
        if (!(spi_id = fork())) {
                int fd;
                unsigned char tx_buf[2];
                unsigned char rx_buf[2];
                struct spi_ioc_transfer xfer[1] = {0};
                uint8_t mode = SPI_MODE_3, bits = 8;
                uint32_t speed = 2000000;
                int rc;
                fd = open(SPI_PATH, O_RDWR);
                if (fd < 0) {
                        printf("Failed to start SPI\n");
                        close(fd);
                        return -1;
                }
                rc = ioctl(fd, SPI_IOC_WR_MODE, &mode);
                if (rc < 0) {
                        printf("Failed to set SPI read mode\n");
                        return rc;
                }
                rc = ioctl(fd, SPI_IOC_RD_MODE, &mode);
                if (rc < 0) {
                        printf("Failed to set SPI write mode\n");
                        return rc;
                }
                rc = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
                if (rc < 0) {
                        printf("Failed to set the SPI write bits-per word\n");
                        return rc;
                }
                rc = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
                if (rc < 0) {
                        printf("Failed to set the SPI read bits-per word\n");
                        return rc;
                }
                rc = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
                if (rc < 0) {
                        printf("Failed to set SPI write speed\n");
                        return rc;
                }
                rc = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
                if (rc < 0) {
                        printf("Failed to set SPI read speed\n");
                        return rc;
                }
                printf("SPI mode: 0x%x\n", mode);
                printf("Bits per word: %d\n", bits);
                printf("Max speed: %d Hz (%d KHz)\n", speed, speed / 1000);
                sigset_t mask;
                pause();
                while (1) {
                        if (ioctl(fd, SPI_IOC_MESSAGE(1), xfer) < 0)
                                perror("SPI_IOC_MESSAGE");
                        else if (rx_buf[0] != 0x00) {
                                /* the sigval represents the modifications to be
                                 *made to the input bitmask the 8 least
                                 *significant bits represent bitmask1 and
                                 *bitmask0, consecutively They are followed by 1
                                 *more set of bitmask1 and bitmask0 then the
                                 *address for the first set of bitmasks ( the
                                 *group no. where the change will be
                                 *implemented) and finally the address for the
                                 *second set of bitmasks address 1 = 0x01 (group
                                 *no. 0 + half 1)
                                 *address 0 = 0x00 (group no. 0 + half 0))
                                 *bitmask1 1 = 0xF
                                 *bitmask0 1 = 0x0
                                 * bitmask1 0 = 0xF
                                 * bitmask0 0 = 0x1 */
                                /* unsigned int sigv = 0x0100E0E1; */
                                sigqueue(getppid(), SIGUSR1,
                                         (union sigval)0x0100F0F1);
                        }
                }
        } else if (spi_id < 0) {
                goto error;
        } else if (!(rotate_state.pid = fork())) {
                uint16_t *buf, *buf2;
                int duty_cycle;
                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGTSTP);
                struct sigaction act;
                act.sa_sigaction = sigstophandler;
                act.sa_flags = SA_SIGINFO;
                act.sa_mask = mask;
                if (sigaction(SIGTSTP, &act, NULL) < 0) {
                        perror("sigaction");
                        exit(1);
                }
                if ((fd = open("/dev/imu", O_RDWR)) < 0) {
                        perror("open");
                        exit(1);
                }
                raise(SIGSTOP);
                while (1) {
                        if (flag == 1) {
                                flag = 0;
                                raise(SIGSTOP);
                        }
                        if (read(fd, buf, 3) < 0) {
                                perror("read");
                                exit(1);
                        }
                        kalmanfilterR(buf, buf2); // rotate kalman filter
                        duty_cycle = pid(buf2);   // rotate pid
                        if (pwm(duty_cycle) < 0) {
                                perror("pwm");
                                exit(1);
                        }
                }
        } else if (rotate_state.pid < 0) {
                goto error;
        } else if (!(stationary_state.pid = fork())) {
                uint16_t *buf, *buf2;
                int duty_cycle;
                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGTSTP);
                struct sigaction act;
                act.sa_sigaction = sigstophandler;
                act.sa_flags = SA_SIGINFO;
                act.sa_mask = mask;
                if (sigaction(SIGTSTP, &act, NULL) < 0) {
                        perror("sigaction");
                        exit(1);
                }
                if ((fd = open("/dev/imu", O_RDWR)) < 0) {
                        perror("open");
                        exit(1);
                }
                flag = 1;
                raise(SIGSTOP);
                while (1) {
                        if (flag == 1) {
                                flag = 0;
                                raise(SIGSTOP);
                        }
                        if (read(fd, buf, 3) < 0) {
                                perror("read");
                                exit(1);
                        }
                        kalmanfilterS(buf, buf2); // rotate kalman filter
                        duty_cycle = pid(buf2);   // rotate pid
                        if (pwm(duty_cycle) < 0) {
                                perror("pwm");
                                exit(1);
                        }
                }
        } else if (stationary_state.pid < 0) {
                goto error;
        } else { // parent
                init_modes();
                int status;
                struct sigaction act;
                act.sa_sigaction = sigusrhandler;
                act.sa_flags = SA_SIGINFO;
                if (sigaction(SIGUSR1, &act, NULL) < 0) {
                        perror("sigaction");
                        exit(1);
                }
                for(int i = 0; i < NO_OF_STATES + NO_OF_READERS; i++) {
                        if (waitpid(-1, &status, WUNTRACED) < 0) {
                                perror("waitpid");
                                exit(1);
                        }
                        printf("waited: %d\n", i);
                        if (!WIFSTOPPED(status)) {
                                perror("waitpid");
                                exit(1);
                        }
                }
                kill(rotate_state.pid, SIGCONT);
                if (waitpid(rotate_state.pid, &status, WCONTINUED) < 0) {
                        perror("waitpid");
                        exit(1);
                }
                if (WIFCONTINUED(status)) {
                        current_state = (next_state = &rotate_state);
                }
                kill(spi_id, SIGCONT);
                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGCLD);
                for (;;) {
                        pause();
                }
        }
error:
        perror("fork");
        exit(1);
}
