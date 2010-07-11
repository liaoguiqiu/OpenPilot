/**
 * \file display_qt.chpp
 * \author croussil@laas.fr
 * \date 25/03/2010
 * \ingroup rtslam
 */

#ifdef HAVE_MODULE_GDHE

#include "rtslam/display_gdhe.hpp"
#include "rtslam/quatTools.hpp"
#include "rtslam/ahpTools.hpp"

#include "jmath/angle.hpp"

namespace jafar {
namespace rtslam {
namespace display {


	WorldGdhe::WorldGdhe(ViewerAbstract *_viewer, rtslam::WorldAbstract *_slamWor, WorldDisplay *garbage):
		WorldDisplay(_viewer, _slamWor, garbage), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer))
	{
		
	}
	
	MapGdhe::MapGdhe(ViewerAbstract *_viewer, rtslam::MapAbstract *_slamMap, WorldGdhe *_dispWorld):
		MapDisplay(_viewer, _slamMap, _dispWorld), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer)), frame(1)
	{
		frame.setColor(216,216,216);
		viewerGdhe->client.addObject(&frame, true);
	}
		
	void MapGdhe::bufferize()
	{
		poseQuat = ublas::subrange(slamMap_->state.x(), 0, 7);
	}
	
	void MapGdhe::render()
	{
		#if 0
		jblas::vec poseEuler(6);
		ublas::subrange(poseEuler,0,3) = ublas::subrange(poseQuat,0,3);
		ublas::subrange(poseEuler,3,6) = quaternion::q2e(ublas::subrange(poseQuat,3,7));
		for(int i = 3; i < 6; ++i) poseEuler(i) = jmath::radToDeg(poseEuler(i));
		std::swap(poseEuler(3), poseEuler(5)); // FIXME the common convention is yaw pitch roll, not roll pitch yaw...
		
		frame.setPose(poseEuler);
		frame.refresh();
		#endif
	}
	
	
	RobotGdhe::RobotGdhe(ViewerAbstract *_viewer, rtslam::RobotAbstract *_slamRob, MapGdhe *_dispMap):
		RobotDisplay(_viewer, _slamRob, _dispMap), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer)), robot(viewerGdhe->robot_model), traj()
	{
		traj.setColor(0,255,0);
		viewerGdhe->client.addObject(&robot, false);
		viewerGdhe->client.addObject(&traj, false);
	}
	
	RobotGdhe::~RobotGdhe()
	{
	}
	
	void RobotGdhe::bufferize()
	{
		poseQuat = slamRob_->pose.x();
	}
	
	void RobotGdhe::render()
	{
//std::cout << "#### New FRAME" << std::endl;
		// robot : convert pose from quat to euler degrees
		jblas::vec poseEuler(6);
		ublas::subrange(poseEuler,0,3) = ublas::subrange(poseQuat,0,3);
		ublas::subrange(poseEuler,3,6) = quaternion::q2e(ublas::subrange(poseQuat,3,7));
		for(int i = 3; i < 6; ++i) poseEuler(i) = jmath::radToDeg(poseEuler(i));
		std::swap(poseEuler(3), poseEuler(5)); // FIXME the common convention is yaw pitch roll, not roll pitch yaw...
		robot.setPose(poseEuler);
		//viewerGdhe->client.setCameraTarget(poseEuler(0), poseEuler(1), poseEuler(2));
		robot.refresh();
		
		// trajectory
		traj.addPoint(poseQuat(0),poseQuat(1),poseQuat(2));
		traj.refresh();
	}
	
	SensorGdhe::SensorGdhe(ViewerAbstract *_viewer, rtslam::SensorAbstract *_slamRob, RobotGdhe *_dispMap):
		SensorDisplay(_viewer, _slamRob, _dispMap), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer)) {}
	
	LandmarkGdhe::LandmarkGdhe(ViewerAbstract *_viewer, rtslam::LandmarkAbstract *_slamLmk, MapGdhe *_dispMap):
		LandmarkDisplay(_viewer, _slamLmk, _dispMap), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer))
	{
		id_ = _slamLmk->id();
		lmkType_ = _slamLmk->type;
		state_.resize(_slamLmk->state.x().size());
		cov_.resize(_slamLmk->state.P().size1(),_slamLmk->state.P().size2());
	}
	
	LandmarkGdhe::~LandmarkGdhe()
	{
		for(ItemList::iterator it = items_.begin(); it != items_.end(); ++it)
			delete *it;
	}

	
	void LandmarkGdhe::bufferize()
	{
		uchar *events = (uchar*)&events_;
		memset(events, 0, sizeof(ObservationAbstract::Events));
		for(LandmarkAbstract::ObservationList::iterator obs = slamLmk_->observationList().begin(); obs != slamLmk_->observationList().end(); ++obs)
		{
			uchar *obsevents = (uchar*)&((*obs)->events);
			for(int i = 0; i < sizeof(ObservationAbstract::Events); i++) events[i] |= obsevents[i];
		}
/*		
		events_.predicted_ = events_.visible_ = events_.measured_ = events_.matched_ = events_.updated_ = false;
		for(LandmarkAbstract::ObservationList::iterator obs = slamLmk_->observationList().begin(); obs != slamLmk_->observationList().end(); ++obs)
		{
			events_.predicted |= (*obs)->events.predicted;
			events_.visible |= (*obs)->events.visible;
			events_.measured |= (*obs)->events.measured;
			events_.matched |= (*obs)->events.matched;
			events_.updated |= (*obs)->events.updated;
		}
*/		
		state_ = slamLmk_->state.x();
		cov_ = slamLmk_->state.P();
	}
	
	void LandmarkGdhe::render()
	{
		const double sph_radius = 0.01;
		switch (lmkType_)
		{
			case LandmarkAbstract::PNT_EUC:
			{
				// Build display objects if it is the first time they are displayed
				if (items_.size() != 1)
				{
					// clear
					items_.clear();

/*					// sphere
					gdhe::Sphere *sph = new gdhe::Sphere(0.01, 12);
					sph->setLabel("");
					items_.push_back(sph);
					viewerGdhe->client.addObject(sph, false);
*/
					// ellipsoid
					gdhe::Ellipsoid *ell = new gdhe::Ellipsoid(12);
					ell->setLabel("");
					items_.push_back(ell);
					viewerGdhe->client.addObject(ell, false);
				}
				// Refresh the display objects every time
				{
					colorRGB c; c.set(255,255,255);

/*					// sphere
					ItemList::iterator it = items_.begin();
					gdhe::Sphere *sph = PTR_CAST<gdhe::Sphere*>(*it);
					sph->setRadius(sph_radius);
					c = getColorRGB(ColorManager::getColorObject_prediction(phase_,events_)) ;
					(*it)->setColor(c.R,c.G,c.B); //
					jblas::vec3 position = lmkAHP::ahp2euc(state_);
					(*it)->setPose(position(0), position(1), position(2), 0, 0, 0);
					(*it)->setLabelColor(c.R,c.G,c.B);
					(*it)->setLabel(jmath::toStr(id_));
					(*it)->refresh();
*/				
					// ellipsoid
					ItemList::iterator it = items_.begin();
					gdhe::Ellipsoid *ell = PTR_CAST<gdhe::Ellipsoid*>(*it);
					ell->set(state_, cov_, viewerGdhe->ellipsesScale);
					c = getColorRGB(ColorManager::getColorObject_prediction(phase_,events_)) ;
					(*it)->setColor(c.R,c.G,c.B); //
					(*it)->setLabelColor(c.R,c.G,c.B);
					(*it)->setLabel(jmath::toStr(id_));
					(*it)->refresh();
				}
				break;
			}
			case LandmarkAbstract::PNT_AH:
			{
				// Build display objects if it is the first time they are displayed
				if (items_.size() != 2)
				{
					// clear
					items_.clear();

/*					// sphere
					gdhe::Sphere *sph = new gdhe::Sphere(sph_radius, 12);
					sph->setLabel("");
					items_.push_back(sph);
					viewerGdhe->client.addObject(sph, false);
*/					
					// ellipsoid
					gdhe::Ellipsoid *ell = new gdhe::Ellipsoid(12);
					ell->setLabel("");
					items_.push_back(ell);
					viewerGdhe->client.addObject(ell, false);
					
					// segment
					gdhe::Polyline *seg = new gdhe::Polyline();
					items_.push_back(seg);
					viewerGdhe->client.addObject(seg, false);
				}
				// Refresh the display objects every time
				{
					colorRGB c; c.set(255,255,255);
/*
					// sphere
					ItemList::iterator it = items_.begin();
					gdhe::Sphere *sph = PTR_CAST<gdhe::Sphere*>(*it);
					sph->setRadius(sph_radius);
					c = getColorRGB(ColorManager::getColorObject_prediction(phase_,events_)) ;
					(*it)->setColor(c.R,c.G,c.B); //
					jblas::vec3 position = lmkAHP::ahp2euc(state_);
					(*it)->setPose(position(0), position(1), position(2), 0, 0, 0);
					(*it)->setLabelColor(c.R,c.G,c.B);
					(*it)->setLabel(jmath::toStr(id_));
					(*it)->refresh();
					*/
					// ellipsoid
					ItemList::iterator it = items_.begin();
					gdhe::Ellipsoid *ell = PTR_CAST<gdhe::Ellipsoid*>(*it);
					jblas::vec xNew; jblas::sym_mat pNew; slamLmk_->reparametrize(LandmarkEuclideanPoint::size(), xNew, pNew);
//std::cout << "x_ahp " << state_ << " P_ahp " << cov_ << " ; x_euc " << xNew << " P_euc " << pNew << std::endl;
					ell->setCompressed(xNew, pNew, viewerGdhe->ellipsesScale);
//					ell->set(xNew, pNew, viewerGdhe->ellipsesScale);
					c = getColorRGB(ColorManager::getColorObject_prediction(phase_,events_)) ;
					(*it)->setColor(c.R,c.G,c.B); //
					(*it)->setLabelColor(c.R,c.G,c.B);
					(*it)->setLabel(jmath::toStr(id_));
					(*it)->refresh();
					
					
					// segment
					++it;
					gdhe::Polyline *seg = PTR_CAST<gdhe::Polyline*>(*it);
					seg->clear();
					double id_std = sqrt(cov_(6,6))*viewerGdhe->ellipsesScale;
					jblas::vec3 position = lmkAHP::ahp2euc(state_);
					jblas::vec7 state = state_; 
					state(6) = state_(6) - id_std; if (state(6) < 1e-2) state(6) = 1e-2;
					jblas::vec3 positionExt = lmkAHP::ahp2euc(state);
					seg->addPoint(positionExt(0)-position(0), positionExt(1)-position(1), positionExt(2)-position(2));
					state(6) = state_(6) + id_std;
					positionExt = lmkAHP::ahp2euc(state);
					seg->addPoint(positionExt(0)-position(0), positionExt(1)-position(1), positionExt(2)-position(2));
					(*it)->setColor(c.R,c.G,c.B);
					(*it)->setPose(position(0), position(1), position(2), 0, 0, 0);
					(*it)->refresh();
				}
				break;
			}
			
			default:
				JFR_ERROR(RtslamException, RtslamException::UNKNOWN_FEATURE_TYPE, "Don't know how to display this type of landmark: " << type_);
		}
	}

	
	
	ObservationGdhe::ObservationGdhe(ViewerAbstract *_viewer, rtslam::ObservationAbstract *_slamLmk, SensorGdhe *_dispMap):
		ObservationDisplay(_viewer, _slamLmk, _dispMap), viewerGdhe(PTR_CAST<ViewerGdhe*>(_viewer)) {}

}}}

#endif



