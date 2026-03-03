#pragma once 

class IWidget {
public:
	virtual ~IWidget() = default;
	virtual void Render() = 0; 
};