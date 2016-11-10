/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2016 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include <osgEarth/MapFrame>
#include <osgEarth/Cache>
#include <osgEarth/ElevationPool>

using namespace osgEarth;

#define LC "[MapFrame] "

MapFrame::MapFrame() :
_initialized    ( false ),
_highestMinLevel( 0 ),
_mapInfo       ( 0L ),
_parts(Map::ENTIRE_MODEL)
{
    //nop
}

MapFrame::MapFrame(const MapFrame& rhs) :
_initialized         ( rhs._initialized ),
_map                 ( rhs._map.get() ),
_mapInfo             ( rhs._mapInfo ),
_parts               ( rhs._parts ),
_highestMinLevel     ( rhs._highestMinLevel ),
_mapDataModelRevision( rhs._mapDataModelRevision ),
_layers              ( rhs._layers )
{
    //no sync required here; we copied the arrays etc
}

MapFrame::MapFrame(const Map* map) :
_initialized    ( false ),
_map            ( map ),
_mapInfo        ( map ),
_parts          ( Map::ENTIRE_MODEL ),
_highestMinLevel( 0 )
{
    sync();
}

MapFrame::MapFrame(const Map* map, Map::ModelParts parts) :
_initialized    ( false ),
_map            ( map ),
_mapInfo        ( map ),
_parts          ( parts ),
_highestMinLevel( 0 )
{
    sync();
}

bool
MapFrame::isValid() const
{
    return _map.valid();
}

void
MapFrame::setMap(const Map* map)
{
    _layers.clear();

    _map = map;
    if ( map )
        _mapInfo = MapInfo(map);

    _initialized = false;
    _highestMinLevel = 0;

    sync();
}

ElevationPool*
MapFrame::getElevationPool() const
{
    return _map->getElevationPool();
}

bool
MapFrame::sync()
{
    bool changed = false;
    _elevationLayers.clear();

    osg::ref_ptr<const Map> map;
    if ( _map.lock(map) )
    {
        changed = _map->sync( *this );
        if ( changed )
        {
            refreshComputedValues();
        }
    }
    else
    {
        _layers.clear();
    }

    for (LayerVector::const_iterator i = _layers.begin(); i != _layers.end(); ++i)
    {
        ElevationLayer* e = dynamic_cast<ElevationLayer*>(i->get());
        if (e)
            _elevationLayers.push_back(e);
    }
    

    return changed;
}


bool
MapFrame::needsSync() const
{
    if ( !isValid() )
        return false;

    osg::ref_ptr<const Map> map;
    return 
        _map.lock(map) &&
        (map->getDataModelRevision() != _mapDataModelRevision || !_initialized);
}

UID
MapFrame::getUID() const
{
    osg::ref_ptr<const Map> map;
    if ( _map.lock(map) )
        return map->getUID();
    else
        return (UID)0;
}

bool
MapFrame::containsLayer(UID uid) const
{
    for (LayerVector::const_iterator i = _layers.begin(); i != _layers.end(); ++i)
        if (i->get()->getUID() == uid)
            return true;
    return false;
}

void
MapFrame::refreshComputedValues()
{
    // cache the min LOD based on all image/elev layers
    _highestMinLevel = 0;

    for (LayerVector::const_iterator i = _layers.begin(); i != _layers.end(); ++i)
    {
        TerrainLayer* terrainLayer = dynamic_cast<TerrainLayer*>(i->get());
        if (terrainLayer)
        {
            const optional<unsigned>& minLevel = terrainLayer->getTerrainLayerRuntimeOptions().minLevel();
            if (minLevel.isSet() && minLevel.value() > _highestMinLevel)
            {
                _highestMinLevel = minLevel.value();
            }
        }
    }
}

bool
MapFrame::populateHeightField(osg::ref_ptr<osg::HeightField>& hf,
                              const TileKey&                  key,
                              bool                            convertToHAE,
                              ProgressCallback*               progress) const
{
    osg::ref_ptr<const Map> map;
    if ( _map.lock(map) )
    {        
        ElevationInterpolation interp = map->getMapOptions().elevationInterpolation().get();    
        return _elevationLayers.populateHeightField(
            hf.get(),
            key,
            convertToHAE ? map->getProfileNoVDatum() : 0L,
            interp,
            progress );
    }
    else
    {
        return false;
    }
}

bool
MapFrame::isCached( const TileKey& key ) const
{
    // is there a map cache at all?
    if ( _map.valid() && _map->getCache() == 0L )
        return false;

    for (LayerVector::const_iterator i = _layers.begin(); i != _layers.end(); ++i)
    {
        TerrainLayer* layer = dynamic_cast<TerrainLayer*>(i->get());
        if (layer)
        {
            if (!layer->getEnabled())
                continue;

            // If we're cache only we should be fast
            if (layer->getCacheSettings()->cachePolicy()->isCacheOnly())
                continue;

            // no-cache? always slow
            if (layer->getCacheSettings()->cachePolicy()->isCacheDisabled())
                return false;

            // No tile source? skip it
            osg::ref_ptr< TileSource > source = layer->getTileSource();
            if (!source.valid())
                continue;

            //If the tile is blacklisted, it should also be fast.
            if (source->getBlacklist()->contains(key))
                continue;

            //If no data is available on this tile, we'll be fast
            if (!source->hasData(key))
                continue;

            if (!layer->isCached(key))
                return false;
        }
    }
    return true;
}

const MapOptions&
MapFrame::getMapOptions() const
{
    return _map->getMapOptions();
}
