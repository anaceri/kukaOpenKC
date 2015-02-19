#include "KukaLwr.h"
#include <boost/assign/list_of.hpp>
#include "WuPotential.h"
#include <unistd.h>
#include "actcontroller.h"
#include "CtrlParam.h"

#define initP_x 0.28
#define initP_y 0.3
#define initP_z 0.25

void KukaLwr::setReference (CBF::FloatVector new_ref){
    if (new_ref.size() != 6){
        std::cerr << "Passing vector of size " << new_ref.size() << " instead of 6" << std::endl;
        return;
    }
    if (0 != pthread_mutex_lock (&primitiveControllerMutex)){
        perror ("CbfPlanner: setReference(): could not lock mutex");
        exit (EXIT_FAILURE);
    }
    currentTaskTargetP->set_reference(new_ref);
    currentTaskReferenceP->set_reference(new_ref);
    if (0 != pthread_mutex_unlock (&primitiveControllerMutex)){
        perror ("CbfPlanner: setReference(): could not unlock mutex");
        exit (EXIT_FAILURE);
    }
}

void KukaLwr::setReference (double* positions){
    CBF::FloatVector ref(6);
    for (int i=0; i < 6; i++)
        ref(i) = positions[i];
    setReference(ref);
}

void KukaLwr::setReference (CBF::Float x, CBF::Float y, CBF::Float z, CBF::Float ra, CBF::Float rb, CBF::Float rc){
    CBF::FloatVector ref(6);
    ref(0) = x;
    ref(1) = y;
    ref(2) = z;
    ref(3) = ra;
    ref(4) = rb;
    ref(5) = rc;
    setReference (ref);
}


void KukaLwr::update_robot_state(){
    KDL::JntArray q = JntArray (7);
    KDL::Frame position;
    KDL::Rotation TM_kdl;
    KDL::Vector position_p_kdl;
    for (int i=0; i < LBR_MNJ; i++){
        q(i) = jnt_position_act[i];
    }
    worldToToolFkSolver->JntToCart(q,position);
    Jac_kdl.resize(7);
    worldToToolJacSolver->JntToJac(q,Jac_kdl);
    TM_kdl = position.M;
    position_p_kdl = position.p;
    conversions::convert(TM_kdl,m_TM_eigen);
    conversions::convert(position_p_kdl, m_p_eigen);
}

void KukaLwr::update_cbf_controller(){
    setReference(cart_command);
    CBF::FloatVector newResourceVector(7);
    for (int i=0; i < LBR_MNJ; i++){
        newResourceVector(i) = jnt_position_act[i];
    }
    kukaResourceP->set(newResourceVector);
    primitiveControllerP->step();
    updates = kukaResourceP->get() - newResourceVector;
}

void KukaLwr::set_joint_command(RobotModeT m){
    if(m == NormalMode){
        double d_updates[7],pupdates[7];
        for(int i = 0; i < 7; i++){
            d_updates[i] = updates(i);
        }
        jlf->get_filtered_value(d_updates,pupdates);
        for(int i = 0; i < 7; i++){
            jnt_command[i] = jnt_position_act[i] + pupdates[i];
            okc_node->jnt_command[i] = jnt_command[i];
        }
    }
    if(m == PsudoGravityCompensation){
        for(int i = 0; i < 7; i++){
            jnt_command[i] = 0.5*(jnt_position_act[i] + okc_node->jnt_position_mea[i]);
            okc_node->jnt_command[i] = jnt_command[i];
        }
    }
}

void KukaLwr::no_move(){
    for(int i = 0; i < 7; i++){
        jnt_command[i] = jnt_position_act[i];
        okc_node->jnt_command[i] = jnt_command[i];
    }
}


void KukaLwr::setAxisStiffnessDamping (double* s, double* d){
    okc_node->set_stiffness(s,d);
}


void KukaLwr::update_robot_stiffness(){
    //before using this function, the stiffness parameter should be updated in the ActController
//    setAxisStiffnessDamping(stiff_ctrlpara.axis_stiffness, stiff_ctrlpara.axis_damping);
}

double KukaLwr::gettimecycle(){
    double t;
    t = okc_node->cycle_time;
    return t;
}


bool KukaLwr::isFinished(){
    //Todo stop several million second
//    okc_sleep_cycletime(okc,robot_id);
    control_period = okc_node->cycle_time;
    usleep(1000*control_period);
    return (primitiveControllerP->finished());
}

//bool KukaLwr::isPseudoConverged(){
//    if (pseudo_converge_count > 100)
//        return true;
//    return false;
//}

void KukaLwr::waitForFinished(){
//    pseudo_converge_count = 0;
    while (!isFinished());
}

void KukaLwr::get_eef_ft(Eigen::Vector3d& f,Eigen::Vector3d& t){
    f[0] = okc_node->ft->x;
    f[1] = okc_node->ft->y;
    f[2] = okc_node->ft->z;
    t[0] = okc_node->ft->c;
    t[1] = okc_node->ft->b;
    t[2] = okc_node->ft->a;
}


void KukaLwr::get_joint_position_act(){
    for (int i=0;i < 7; i++){
        jnt_position_act[i] = okc_node->jnt_position_act[i];
    }
}

void KukaLwr::get_joint_position_mea(double *jnt){
    for (int i=0;i < 7; i++){
        jnt_position_mea[i] = okc_node->jnt_position_mea[i];
        *(jnt+i) = okc_node->jnt_position_mea[i];
    }
}

void KukaLwr::initSubordinateReference(CBF::FloatVector &f){
    f[0] = M_PI/2.0;
    //f[0] = 0.0;
    f[1] = 0.0;
    f[2] = 0.0;
    //f[3] = 0.0;
    f[3] = M_PI/-2.0;
    f[4] = 0.0;
    f[5] = 0.0;
    f[6] = 0.0;
}

void KukaLwr::initReference(CBF::FloatVector &f){
    if (kuka_right == rn){
        f[0] = initP_x;
        f[1] = initP_y;
        f[2] = initP_z;
        f[3] = 0.0;
        f[4] = -0.5*M_PI;
        f[5] = 0.0;
    }
    if (kuka_left == rn){
        f[0] = -1*initP_x;
        f[1] = initP_y;
        f[2] = initP_z;
        f[3] = 0.0;
        f[4] = M_PI/2;
        f[5] = 0.0;
    }
}

void KukaLwr::initKukaResource (){
    CBF::FloatVector* myResourceVector = new CBF::FloatVector (7);
    get_joint_position_act();
    for (int i=0;i < 7; i++)
        (*myResourceVector) (i) = jnt_position_act[i];
    kukaResourceP->set((*myResourceVector));
}

void KukaLwr::initChains(){
    /*
        DH representation referecen paper:Visual Estimation and Control of Robot Manipulating Systems(phd thesis)
    */
    if (kuka_right == rn){
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.31,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.4,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.39,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,0.0,0.078,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::None),Frame(Vector(0, 0, 0.170))));
    }
    if (kuka_left == rn){
        worldToTool.addSegment (Segment(Joint(Joint::None),Frame(Vector(-0.0823, 0.897, 0.2975))));
        worldToTool.addSegment (Segment(Joint(Joint::None),Frame(Rotation(Rotation::RotY(-1.047)))));
        worldToTool.addSegment (Segment(Joint(Joint::None),Frame(Rotation(Rotation::RotZ(2.6180)))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.31,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.4,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.39,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
        worldToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,0.0,0.078,0.0))));
        //please comment the next line code if you are doing the robot calibration.
        worldToTool.addSegment (Segment(Joint(Joint::None),Frame(Vector(0, 0, 0.170))));
    }
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.31,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.4,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.0,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,M_PI_2,0.39,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,-1.0*M_PI_2,0.0,0.0))));
    baseToTool.addSegment (Segment(Joint(Joint::RotZ),Frame(Frame::DH(0.0,0.0,0.078,0.0))));

    worldToToolFkSolver = new ChainFkSolverPos_recursive (worldToTool);
    baseToToolFkSolver = new ChainFkSolverPos_recursive (baseToTool);
    worldToToolIkSolver = new ChainIkSolverVel_pinv (worldToTool);
    worldToToolJacSolver = new ChainJntToJacSolver (worldToTool);
}

void KukaLwr::initCbf (){
    if (kuka_left == rn){
        boost::shared_ptr<KDL::Chain> chain (new KDL::Chain(worldToTool));
        currentTaskReferenceP = CBF::DummyReferencePtr (new CBF::DummyReference(1,6));
        currentTaskTargetP = CBF::DummyReferencePtr (new CBF::DummyReference(1,6));
        kukaResourceP = CBF::DummyResourcePtr (new CBF::DummyResource(7));
        currentSubordinateTaskReferenceP = CBF::DummyReferencePtr (new CBF::DummyReference(1,7));

        if (0 != pthread_mutex_lock (&primitiveControllerMutex)){
            perror ("CbfPlanner: initCbf(): could not lock mutex");
            exit (EXIT_FAILURE);
        }
        try {
            CBF::FloatVector myReferenceVector(6);
            CBF::FloatVector mySubordinateReferenceVector (7);

            initReference(myReferenceVector);
            currentTaskReferenceP->set_reference(myReferenceVector);
            currentTaskTargetP->set_reference(myReferenceVector);
            initSubordinateReference (mySubordinateReferenceVector);
            currentSubordinateTaskReferenceP->set_reference (mySubordinateReferenceVector);

            double limit = M_PI * (165.0 / 180.0);

            CBF::FloatVector vmins = CBF::FloatVector::Constant(7, -limit);
            CBF::FloatVector vmaxs = CBF::FloatVector::Constant(7,  limit);
            vmins(5) = M_PI* (-150.0 / 180.0);
            vmaxs(5) = M_PI* (150.0/ 180.0);

            std::vector<ConvergenceCriterionPtr> vConvergenceCriteria = boost::assign::list_of
                    (ConvergenceCriterionPtr(new TaskSpaceDistanceThreshold(0.001)))
                    (ConvergenceCriterionPtr(new ResourceStepNormThreshold(0.001)));

            // create the subordinate controller
            subordinateControllerP = SubordinateControllerPtr(new CBF::SubordinateController(
                                                                  1.0,
                                                                  vConvergenceCriteria,
                                                                  currentSubordinateTaskReferenceP,
                                                                  PotentialPtr(new WuPotential(vmins, vmaxs, 0.01)),
                                                                  SensorTransformPtr(new CBF::IdentitySensorTransform(7)),
                                                                  EffectorTransformPtr(new CBF::GenericEffectorTransform(7,7)),
                                                                  std::vector<SubordinateControllerPtr>(),
                                                                  CombinationStrategyPtr(new AddingStrategy())));

            std::vector<CBF::SubordinateControllerPtr> vSubOrdinateControllers;
            // 		vSubOrdinateControllers.push_back(subordinateControllerP);

            // create the composite potential
            xyzSquarePotential = CBF::SquarePotentialPtr(new CBF::SquarePotential(3,0.008));
            xyzSquarePotential->set_max_gradient_step_norm (99.0);
            CBF::AxisAnglePotentialPtr myAxisAnglePotentialP = CBF::AxisAnglePotentialPtr (new CBF::AxisAnglePotential(0.04));
            myAxisAnglePotentialP->set_max_gradient_step_norm (99.0);
            std::vector<CBF::PotentialPtr> myVectorOfPotentials;
            myVectorOfPotentials.push_back(xyzSquarePotential);
            myVectorOfPotentials.push_back(myAxisAnglePotentialP);
            CBF::CompositePotentialPtr myCompositePotentialP = CBF::CompositePotentialPtr (new CBF::CompositePotential(myVectorOfPotentials));
            // create the composite sensor transform
            std::vector<SensorTransformPtr> myVectorOfSensorTransforms = boost::assign::list_of
                    (CBF::SensorTransformPtr(new CBF::KDLChainPositionSensorTransform(chain)))
                    (CBF::SensorTransformPtr(new CBF::KDLChainAxisAngleSensorTransform(chain)));
            CBF::CompositeSensorTransformPtr myCompositeSensorTransformP =
                    CBF::CompositeSensorTransformPtr(new CBF::CompositeSensorTransform(myVectorOfSensorTransforms));

            // create final controller
            primitiveControllerP = PrimitiveControllerPtr(new CBF::PrimitiveController(
                                                              1.0,
                                                              vConvergenceCriteria,
                                                              currentTaskReferenceP,
                                                              myCompositePotentialP,
                                                              myCompositeSensorTransformP,
                                                              EffectorTransformPtr(new CBF::DampedGenericEffectorTransform(6,7, 0.001)),
                                                              vSubOrdinateControllers,
                                                              CombinationStrategyPtr(new AddingStrategy()),
                                                              kukaResourceP));
        }
        catch(...){
            std::cerr << "initCbf(): error: could not initialize CBF!" << std::endl;
            exit (EXIT_FAILURE);
        }
        if (0 != pthread_mutex_unlock (&primitiveControllerMutex)){
            perror ("CbfPlanner: initCbf(): could not unlock mutex");
            exit (EXIT_FAILURE);
        }
        //std::cout << "Distance Threshold " << primitiveControllerP->potential()->m_DistanceThreshold << std::endl;
        //primitiveControllerP->potential()->m_DistanceThreshold = 10000000.0;
        //subordinateControllerP->potential()->m_DistanceThreshold = 999999.9;
        //    noCbf = false;
    }

    if (kuka_right == rn){
        boost::shared_ptr<KDL::Chain> chain (new KDL::Chain(worldToTool));
        currentTaskReferenceP = CBF::DummyReferencePtr (new CBF::DummyReference(1,6));
        currentTaskTargetP = CBF::DummyReferencePtr (new CBF::DummyReference(1,6));
        kukaResourceP = CBF::DummyResourcePtr (new CBF::DummyResource(7));
        currentSubordinateTaskReferenceP = CBF::DummyReferencePtr (new CBF::DummyReference(1,7));

        if (0 != pthread_mutex_lock (&primitiveControllerMutex)){
            perror ("CbfPlanner: initCbf(): could not lock mutex");
            exit (EXIT_FAILURE);
        }
        try {
            CBF::FloatVector myReferenceVector(6);
            CBF::FloatVector mySubordinateReferenceVector (7);

            initReference(myReferenceVector);
            currentTaskReferenceP->set_reference(myReferenceVector);
            currentTaskTargetP->set_reference(myReferenceVector);
            initSubordinateReference (mySubordinateReferenceVector);
            currentSubordinateTaskReferenceP->set_reference (mySubordinateReferenceVector);

            double limit = M_PI * (165.0 / 180.0);

            CBF::FloatVector vmins = CBF::FloatVector::Constant(7, -limit);
            CBF::FloatVector vmaxs = CBF::FloatVector::Constant(7,  limit);
            vmins(5) = M_PI* (-150.0 / 180.0);
            vmaxs(5) = M_PI* (150.0/ 180.0);

            std::vector<ConvergenceCriterionPtr> vConvergenceCriteria = boost::assign::list_of
                    (ConvergenceCriterionPtr(new TaskSpaceDistanceThreshold(0.001)))
                    (ConvergenceCriterionPtr(new ResourceStepNormThreshold(0.001)));

            // create the subordinate controller
            subordinateControllerP = SubordinateControllerPtr(new CBF::SubordinateController(
                                                                  1.0,
                                                                  vConvergenceCriteria,
                                                                  currentSubordinateTaskReferenceP,
                                                                  PotentialPtr(new WuPotential(vmins, vmaxs, 0.01)),
                                                                  SensorTransformPtr(new CBF::IdentitySensorTransform(7)),
                                                                  EffectorTransformPtr(new CBF::GenericEffectorTransform(7,7)),
                                                                  std::vector<SubordinateControllerPtr>(),
                                                                  CombinationStrategyPtr(new AddingStrategy())));

            std::vector<CBF::SubordinateControllerPtr> vSubOrdinateControllers;
            // 		vSubOrdinateControllers.push_back(subordinateControllerP);

            // create the composite potential
            xyzSquarePotential = CBF::SquarePotentialPtr(new CBF::SquarePotential(3,0.008));
            xyzSquarePotential->set_max_gradient_step_norm (99.0);
            CBF::AxisAnglePotentialPtr myAxisAnglePotentialP = CBF::AxisAnglePotentialPtr (new CBF::AxisAnglePotential(0.04));
            myAxisAnglePotentialP->set_max_gradient_step_norm (99.0);
            std::vector<CBF::PotentialPtr> myVectorOfPotentials;
            myVectorOfPotentials.push_back(xyzSquarePotential);
            myVectorOfPotentials.push_back(myAxisAnglePotentialP);
            CBF::CompositePotentialPtr myCompositePotentialP = CBF::CompositePotentialPtr (new CBF::CompositePotential(myVectorOfPotentials));
            // create the composite sensor transform
            std::vector<SensorTransformPtr> myVectorOfSensorTransforms = boost::assign::list_of
                    (CBF::SensorTransformPtr(new CBF::KDLChainPositionSensorTransform(chain)))
                    (CBF::SensorTransformPtr(new CBF::KDLChainAxisAngleSensorTransform(chain)));
            CBF::CompositeSensorTransformPtr myCompositeSensorTransformP =
                    CBF::CompositeSensorTransformPtr(new CBF::CompositeSensorTransform(myVectorOfSensorTransforms));

            // create final controller
            primitiveControllerP = PrimitiveControllerPtr(new CBF::PrimitiveController(
                                                              1.0,
                                                              vConvergenceCriteria,
                                                              currentTaskReferenceP,
                                                              myCompositePotentialP,
                                                              myCompositeSensorTransformP,
                                                              EffectorTransformPtr(new CBF::DampedGenericEffectorTransform(6,7, 0.001)),
                                                              vSubOrdinateControllers,
                                                              CombinationStrategyPtr(new AddingStrategy()),
                                                              kukaResourceP));
        }
        catch(...){
            std::cerr << "initCbf(): error: could not initialize CBF!" << std::endl;
            exit (EXIT_FAILURE);
        }
        if (0 != pthread_mutex_unlock (&primitiveControllerMutex)){
            perror ("CbfPlanner: initCbf(): could not unlock mutex");
            exit (EXIT_FAILURE);
        }
        //std::cout << "Distance Threshold " << primitiveControllerP->potential()->m_DistanceThreshold << std::endl;
        //primitiveControllerP->potential()->m_DistanceThreshold = 10000000.0;
        //subordinateControllerP->potential()->m_DistanceThreshold = 999999.9;
        //    noCbf = false;
    }
}


KukaLwr::KukaLwr(RobotNameT robotname, ComOkc& com)
{
    if (0 != pthread_mutex_init(&primitiveControllerMutex,NULL)){
        perror ("CbfPlanner: could not initialize Mutex");
        exit (EXIT_FAILURE);
    }
    rn = robotname;
    okc_node = &com;
    initChains();
    initCbf();
    control_period = 4;
    Jac_kdl = KDL::Jacobian (7);
    updates.resize(7);
    for(int i = 0; i < 7; i++){
        updates(i) = 0.0;
    }
    jlf = new JntLimitFilter(okc_node->cycle_time);
}