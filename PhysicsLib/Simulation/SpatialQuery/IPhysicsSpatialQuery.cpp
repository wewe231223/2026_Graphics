#include "IPhysicsSpatialQuery.h"

IPhysicsSpatialQuery::IPhysicsSpatialQuery() {
}

IPhysicsSpatialQuery::~IPhysicsSpatialQuery() {
}

IPhysicsSpatialQuery::IPhysicsSpatialQuery(const IPhysicsSpatialQuery& Other) {
    (void)Other;
}

IPhysicsSpatialQuery& IPhysicsSpatialQuery::operator=(const IPhysicsSpatialQuery& Other) {
    (void)Other;
    return *this;
}

IPhysicsSpatialQuery::IPhysicsSpatialQuery(IPhysicsSpatialQuery&& Other) noexcept {
    (void)Other;
}

IPhysicsSpatialQuery& IPhysicsSpatialQuery::operator=(IPhysicsSpatialQuery&& Other) noexcept {
    (void)Other;
    return *this;
}
