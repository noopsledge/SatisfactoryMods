#include "FGModTrainStationRepresentation.h"

UFGModTrainStationRepresentation::UFGModTrainStationRepresentation()
{
	// The real actor (AFGTrainStationIdentifier) is always available on all clients, but the station
	// that it references isn't. We therfore can't rely on its implementation of GetRealActorLocation
	// because that will try to ask the (potentially non-existent) station for its transform.
	mAllowRealActorLocationOnClient = false;
}
