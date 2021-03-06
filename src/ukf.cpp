#include "ukf.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(5);

  // initial covariance matrix
  P_ = MatrixXd(5, 5);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 1;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = 0.8;


  // NOTE: The following noise parameters are given by sensor manufacturer
  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  // State has not been initialized
  is_initialized_ = false;

  // Set state dimension
  n_x_ = 5;

  // Set augmented dimension
  n_aug_ = 7;

  // Define spreading parameter
  lambda_ = 3 - n_aug_;

  R_laser = MatrixXd(2,2);
  R_laser.fill(0.0);
  R_laser(0,0)  = std_laspx_*std_laspx_;
  R_laser(1,1)  = std_laspy_*std_laspy_;

  R_radar = MatrixXd(3,3);
  R_radar.fill(0.0);
  R_radar(0,0)  = std_radr_*std_radr_;
  R_radar(1,1)  = std_radphi_*std_radphi_;
  R_radar(2,2)  = std_radrd_*std_radrd_;

  H_laser_ = MatrixXd(2, 5);
  H_laser_ << 1, 0, 0, 0, 0,
              0, 1, 0, 0, 0;

  // Set vector of weights
  weights_ = VectorXd(2*n_aug_+1);
  weights_(0) = lambda_ / (lambda_ + n_aug_);;
  for (int i=1; i<2*n_aug_+1; i++) {
      weights_(i) = 0.5 / (n_aug_ + lambda_);;
  }

  // Create matrix for predicted sigma points
  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  // Set initial timestamp
  time_us_ = 0;

  // Initialize state covariance matrix
  P_ = MatrixXd::Identity(5,5);

  NIS_radar_ = 0;
  NIS_laser_ = 0;
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /*****************************************************************************
  *  Initialization
  ****************************************************************************/
  if (!is_initialized_) {
    // first measurement
    cout << "Initializing state: " << endl;
    x_ << 1, 1, 1, 1, 1;

    if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
        /**
        Convert radar from polar to cartesian coordinates and initialize state.
        */
        float rho = meas_package.raw_measurements_[0];
        float phi = meas_package.raw_measurements_[1];
        x_ << rho*cos(phi), rho*sin(phi), 0, 0, 0;
    }
    else if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
        /**
        Initialize state.
        */
        x_ << meas_package.raw_measurements_[0], meas_package.raw_measurements_[1], 0, 0, 0;
        P_ << std_laspx_*std_laspx_, 0, 0, 0, 0,
              0, std_laspy_*std_laspy_, 0, 0, 0,
              0, 0, 1, 0, 0,
              0, 0, 0, 1, 0,
              0, 0, 0, 0, 1;
    }

    // done initializing, no need to predict or update
    is_initialized_ = true;
    time_us_ = meas_package.timestamp_;

    return;
  }

  if (use_laser_ and meas_package.sensor_type_ == MeasurementPackage::LASER){
      // Compute delta_t
      float dt = (meas_package.timestamp_ - time_us_) / 1000000.0;	//dt - expressed in seconds
      time_us_ = meas_package.timestamp_;

      Prediction(dt);

      // Laser updates
      UpdateLidar(meas_package);
  }
  else if(use_radar_ and meas_package.sensor_type_ == MeasurementPackage::RADAR){
      // Compute delta_t
      float dt = (meas_package.timestamp_ - time_us_) / 1000000.0;	//dt - expressed in seconds
      time_us_ = meas_package.timestamp_;

      Prediction(dt);

      // Radar updates
      UpdateRadar(meas_package);
  }

  // print the output
  cout << "x_ = " << x_ << endl;
  cout << "P_ = " << P_ << endl;
}

void UKF::NormalizeAngle(double& angle)
{
  while (angle> M_PI) angle-=2.*M_PI;
  while (angle<-M_PI) angle+=2.*M_PI;
}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {
  //create augmented mean vector
  VectorXd x_aug = VectorXd(n_aug_);

  //create augmented state covariance
  MatrixXd P_aug = MatrixXd(n_aug_, n_aug_);

  //create augmented mean state
  x_aug.head(n_x_) = x_;
  x_aug.tail(n_aug_-n_x_) << 0, 0;

  //create augmented covariance matrix
  P_aug.fill(0);
  P_aug.topLeftCorner(n_x_,n_x_) = P_;
  P_aug.bottomRightCorner(n_aug_-n_x_,n_aug_-n_x_) << std_a_*std_a_, 0, 0, std_yawdd_*std_yawdd_;

  //calculate square root of P_aug
  MatrixXd A = P_aug.llt().matrixL();

  //create sigma point matrix
  MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);

  // Create augmented sigma points
  Xsig_aug.col(0) = x_aug;
  for(int i=0; i<n_aug_; i++){
      Xsig_aug.col(i+1) = x_aug + sqrt(lambda_+n_aug_)*A.col(i);
      Xsig_aug.col(i+1+n_aug_) = x_aug - sqrt(lambda_+n_aug_)*A.col(i);
  }

  // Predict sigma points
  for(int i=0; i<2 * n_aug_ + 1; i++){
      const float px = Xsig_aug.col(i)(0);
      const float py = Xsig_aug.col(i)(1);
      const float v = Xsig_aug.col(i)(2);
      const float yaw = Xsig_aug.col(i)(3);
      const float yaw_dot = Xsig_aug.col(i)(4);
      const float noise_a = Xsig_aug.col(i)(5);
      const float noise_yaw = Xsig_aug.col(i)(6);

      //predicted state values
      double px_p, py_p;

      //avoid division by zero
      if (fabs(yaw_dot) > 0.001) {
          px_p = px + v/yaw_dot * ( sin (yaw + yaw_dot*delta_t) - sin(yaw));
          py_p = py + v/yaw_dot * ( cos(yaw) - cos(yaw+yaw_dot*delta_t) );
      }
      else {
          px_p = px + v*delta_t*cos(yaw);
          py_p = py + v*delta_t*sin(yaw);
      }

      double v_p = v;
      double yaw_p = yaw + yaw_dot*delta_t;
      double yawd_p = yaw_dot;

      //add noise
      px_p = px_p + 0.5*noise_a*delta_t*delta_t * cos(yaw);
      py_p = py_p + 0.5*noise_a*delta_t*delta_t * sin(yaw);
      v_p = v_p + noise_a*delta_t;

      yaw_p = yaw_p + 0.5*noise_yaw*delta_t*delta_t;
      yawd_p = yawd_p + noise_yaw*delta_t;

      Xsig_pred_.col(i) << px_p, py_p, v_p, yaw_p, yawd_p;
  }

  //predict state mean
  x_ = Xsig_pred_ * weights_;

  //predict state covariance matrix
  P_.fill(0);
  for(int i=0; i<2*n_aug_+1; i++){
      // state difference
      VectorXd x_diff = Xsig_pred_.col(i) - x_;

      //angle normalization
      NormalizeAngle(x_diff(3));
      P_ = P_ + weights_(i) * x_diff * x_diff.transpose();
  }

}


/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {


  // For laser measurements
  VectorXd z_pred = H_laser_ * x_;
	VectorXd y = meas_package.raw_measurements_ - z_pred;       // Error term
	MatrixXd Ht = H_laser_.transpose();
	MatrixXd S = H_laser_ * P_ * Ht + R_laser;
	MatrixXd Si = S.inverse();
	MatrixXd PHt = P_ * Ht;
	MatrixXd K = PHt * Si;         // Kalman Gain

  //new estimate
	x_ = x_ + (K * y);
	long x_size = x_.size();
	MatrixXd I = MatrixXd::Identity(x_size, x_size);
  P_ = (I - K * H_laser_) * P_;

  // Calculate NIS
  NIS_laser_ = y.transpose() * S.inverse() * y;
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {
  // Set lidar measurement dimension
  const int n_z = 3;

  //create matrix for sigma points in measurement space
  MatrixXd Zsig = MatrixXd(3, 2 * n_aug_ + 1);

  //transform sigma points into measurement space
  for(int i=0; i<2*n_aug_+1; i++) {
      const float px = Xsig_pred_.col(i)(0);
      const float py = Xsig_pred_.col(i)(1);
      const float v = Xsig_pred_.col(i)(2);
      const float psi = Xsig_pred_.col(i)(3);

      const float rho = sqrt((px*px)+(py*py));
      const float phi = atan2(py,px);
      const float rho_dot = (v*px*cos(psi) + v*py*sin(psi)) / rho;
      Zsig.col(i) << rho, phi, rho_dot;
  }

  //mean predicted measurement
  VectorXd z_pred = VectorXd(n_z);

  //measurement covariance matrix S
  MatrixXd S = MatrixXd(n_z,n_z);

  //calculate mean predicted measurement
  z_pred.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points
      z_pred = z_pred + weights_(i) * Zsig.col(i);
  }
  S.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points
    // state difference
    VectorXd z_diff = Zsig.col(i) - z_pred;
    //angle normalization
    NormalizeAngle(z_diff(1));

    S = S + weights_(i) * z_diff * z_diff.transpose() ;
  }

  S = S + R_radar;

  //create matrix for cross correlation Tc
  MatrixXd Tc = MatrixXd(n_x_, n_z);

  //calculate cross correlation matrix
  Tc.fill(0.0);
  for(int i=0; i<2 * n_aug_ + 1; i++){
    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;
    //angle normalization
    NormalizeAngle(z_diff(1));

    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    //angle normalization
    NormalizeAngle(x_diff(3));

    Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
  }
  //calculate Kalman gain K;
  MatrixXd K = Tc*S.inverse();

  //residual
  VectorXd z_diff = meas_package.raw_measurements_ - z_pred;

  //angle normalization
  NormalizeAngle(z_diff(1));

  //update state mean and covariance matrix
  x_ = x_ + K*z_diff;
  P_ = P_ - K*S*K.transpose();

  // Calculate NIS
  NIS_radar_ = z_diff.transpose() * S.inverse() * z_diff;
}
