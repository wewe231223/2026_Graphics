#pragma once 

class IWidget {
public:
	IWidget() = default;
	virtual void Render() = 0; 
};