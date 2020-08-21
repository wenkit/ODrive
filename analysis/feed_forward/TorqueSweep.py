import odrive
from odrive.enums import *
import numpy as np
import numpy.fft
import math
import scipy as sp
import scipy.signal
from scipy import fftpack
import math
import matplotlib.pyplot as plt
import time

def fftPlot(signal, dT):
    # from stackoverflow
    sigFFT = numpy.fft.rfft(sig)
    freq = numpy.fft.rfftfreq(len(signal), dT)

    return np.abs(sigFFT), freq

if __name__ == "__main__":
    # generate test sequence
    f_start = 1
    f_end = 50
    duration = 10
    fs = 200
    t = np.linspace(0, duration, duration*fs)
    seq = scipy.signal.chirp(t=t, f0=f_start, t1 = duration, f1=f_end, method="logarithmic", phi=90)
    max_current = 4

    # get odrive
    odrv0 = odrive.find_any()

    # setup odrive
    print("Setting up odrive...")
    odrv0.axis0.controller.config.vel_limit = 150
    odrv0.config.brake_resistance = 0.47
    odrv0.axis0.motor.config.current_lim = 30
    odrv0.axis0.motor.config.torque_constant = 8.27 / 270
    odrv0.axis0.requested_state = AXIS_STATE_FULL_CALIBRATION_SEQUENCE
    print("Calibrating odrive...")
    time.sleep(15)
    print("Calibration done!")
    odrv0.axis0.requested_state = AXIS_STATE_CLOSED_LOOP_CONTROL
    odrv0.axis0.controller.config.control_mode = 1 # torque control
    odrv0.axis0.motor.config.bEMF_FF_enable = False
    odrv0.axis0.motor.config.omega_L_FF_enable = False
    # response with no FF
    print("Beginning torque sweep")
    response = []
    stimulus = []
    time_noff = []
    t_start = time.monotonic()
    for i in range(duration * fs):
        _t = time.monotonic()
        odrv0.axis0.controller.input_torque = max_current * 8.27 / 270 * seq[i]
        response.append(odrv0.axis0.motor.current_control.Iq_measured)
        stimulus.append(odrv0.axis0.motor.current_control.Iq_setpoint)
        time_noff.append(time.monotonic() - t_start)
        time.sleep(1 / fs - (time.monotonic() - _t))
    odrv0.axis0.controller.input_torque = 0

    # response with bEMF FF
    print("Beginning torque sweep with bEMF FF enabled")
    odrv0.axis0.motor.config.bEMF_FF_enable = True
    response_bEMF = []
    stimulus_bEMF = []
    time_bEMF = []
    t_start = time.monotonic()
    for i in range(duration * fs):
        _t = time.monotonic()
        odrv0.axis0.controller.input_torque = max_current * 8.27 / 270 * seq[i]
        response_bEMF.append(odrv0.axis0.motor.current_control.Iq_measured)
        stimulus_bEMF.append(odrv0.axis0.motor.current_control.Iq_setpoint)
        time_bEMF.append(time.monotonic() - t_start)
        time.sleep(1 / fs - (time.monotonic() - _t))
    odrv0.axis0.controller.input_torque = 0

    # response with omega_L FF
    print("Beginning torque sweep with omega_L FF enabled")
    odrv0.axis0.motor.config.bEMF_FF_enable = False
    odrv0.axis0.motor.config.omega_L_FF_enable = True
    response_omega_L = []
    stimulus_omega_L = []
    time_omega_L = []
    t_start = time.monotonic()
    for i in range(duration * fs):
        _t = time.monotonic()
        odrv0.axis0.controller.input_torque = max_current * 8.27 / 270 * seq[i]
        response_omega_L.append(odrv0.axis0.motor.current_control.Iq_measured)
        stimulus_omega_L.append(odrv0.axis0.motor.current_control.Iq_setpoint)
        time_omega_L.append(time.monotonic() - t_start)
        time.sleep(1 / fs - (time.monotonic() - _t))
    odrv0.axis0.controller.input_torque = 0


    # torque sweep with both FF enabled
    print("Beginning torque sweep with both FF enabled")
    odrv0.axis0.motor.config.bEMF_FF_enable = True
    odrv0.axis0.motor.config.omega_L_FF_enable = True
    response_both = []
    stimulus_both = []
    time_both = []
    t_start = time.monotonic()
    for i in range(duration * fs):
        _t = time.monotonic()
        odrv0.axis0.controller.input_torque = max_current * 8.27 / 270 * seq[i]
        response_both.append(odrv0.axis0.motor.current_control.Iq_measured)
        stimulus_both.append(odrv0.axis0.motor.current_control.Iq_setpoint)
        time_both.append(time.monotonic() - t_start)
        time.sleep(1 / fs - (time.monotonic() - _t))
    odrv0.axis0.controller.input_torque = 0

    

    # plot results