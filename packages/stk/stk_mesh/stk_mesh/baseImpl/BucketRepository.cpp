/*------------------------------------------------------------------------*/
/*                 Copyright 2010 Sandia Corporation.                     */
/*  Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive   */
/*  license for use of this work by or on behalf of the U.S. Government.  */
/*  Export of this program may require a license from the                 */
/*  United States Government.                                             */
/*------------------------------------------------------------------------*/

#include <sstream>
#include <cstdlib>
#include <stdexcept>

#include <stk_mesh/baseImpl/BucketRepository.hpp>
#include <stk_mesh/baseImpl/EntityRepository.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Bucket.hpp>

namespace stk {
namespace mesh {
namespace impl {

//----------------------------------------------------------------------
namespace {

void * local_malloc( size_t n )
{
  void * const ptr = std::malloc( n );

  if ( NULL == ptr ) {
    std::ostringstream msg ;
    msg << "stk::mesh::impl::BucketImpl::declare_bucket FAILED malloc( " << n << " )" ;
    throw std::runtime_error( msg.str() );
  }

  return ptr ;
}


} // namespace

//----------------------------------------------------------------------

namespace {

inline unsigned align( size_t nb )
{
  enum { BYTE_ALIGN = 16 };
  const unsigned gap = nb % BYTE_ALIGN ;
  if ( gap ) { nb += BYTE_ALIGN - gap ; }
  return nb ;
}

struct FieldRestrictionLess {
  bool operator()( const FieldBase::Restriction & lhs ,
                   const EntityKey & rhs ) const
    { return lhs.key < rhs ; }
};

const FieldBase::Restriction & empty_field_restriction()
{
  static const FieldBase::Restriction empty ;
  return empty ;
}

const FieldBase::Restriction & dimension( const FieldBase & field ,
                                          unsigned etype ,
                                          const unsigned num_part_ord ,
                                          const unsigned part_ord[] ,
                                          const char * const method )
{
  const FieldBase::Restriction & empty = empty_field_restriction();
  const FieldBase::Restriction * dim = & empty ;

  const std::vector<FieldBase::Restriction> & dim_map = field.restrictions();
  const std::vector<FieldBase::Restriction>::const_iterator iend = dim_map.end();
        std::vector<FieldBase::Restriction>::const_iterator ibeg = dim_map.begin();

  for ( unsigned i = 0 ; i < num_part_ord && iend != ibeg ; ++i ) {

    const EntityKey key = EntityKey(etype,part_ord[i]);

    ibeg = std::lower_bound( ibeg , iend , key , FieldRestrictionLess() );

    if ( iend != ibeg && ibeg->key == key ) {
      if ( dim == & empty ) { dim = & *ibeg ; }

      if ( Compare< MaximumFieldDimension >::
             not_equal( ibeg->stride , dim->stride ) ) {

        Part & p_old = field.mesh_meta_data().get_part( ibeg->ordinal() );
        Part & p_new = field.mesh_meta_data().get_part( dim->ordinal() );

        std::ostringstream msg ;
        msg << method ;
        msg << " FAILED WITH INCOMPATIBLE DIMENSIONS FOR " ;
        msg << field ;
        msg << " Part[" << p_old.name() ;
        msg << "] and Part[" << p_new.name() ;
        msg << "]" ;

        throw std::runtime_error( msg.str() );
      }
    }
  }

  return *dim ;
}

} // namespace

//----------------------------------------------------------------------


BucketRepository::BucketRepository(
    BulkData & mesh,
    unsigned bucket_capacity,
    unsigned entity_rank_count,
    EntityRepository & entity_repo
    )
  :m_mesh(mesh),
   m_bucket_capacity(bucket_capacity),
   m_buckets(entity_rank_count),
   m_nil_bucket(NULL),
   m_entity_repo(entity_repo)
{
}


BucketRepository::~BucketRepository()
{
  // Destroy buckets, which were *not* allocated by the set.

  try {
    for ( std::vector< std::vector<Bucket*> >::iterator
          i = m_buckets.end() ; i != m_buckets.begin() ; ) {
      try {
        std::vector<Bucket*> & kset = *--i ;

        while ( ! kset.empty() ) {
          try { destroy_bucket( kset.back() ); } catch(...) {}
          kset.pop_back();
        }
        kset.clear();
      } catch(...) {}
    }
    m_buckets.clear();
  } catch(...) {}

  try { if ( m_nil_bucket ) destroy_bucket( m_nil_bucket ); } catch(...) {}
}


//----------------------------------------------------------------------
// The current 'last' bucket in a family is to be deleted.
// The previous 'last' bucket becomes the new 'last' bucket in the family.

void BucketRepository::destroy_bucket( const unsigned & entity_rank , Bucket * bucket_to_be_deleted )
{
  static const char method[] = "stk::mesh::impl::BucketRepository::destroy_bucket" ;

  m_mesh.mesh_meta_data().assert_entity_rank( method, entity_rank );
  std::vector<Bucket *> & bucket_set = m_buckets[entity_rank];

  Bucket * const first = bucket_to_be_deleted->m_bucketImpl.first_bucket_in_family();

  if ( 0 != bucket_to_be_deleted->size() || bucket_to_be_deleted != first->m_bucketImpl.get_bucket_family_pointer() ) {
    throw std::logic_error(std::string(method));
  }

  std::vector<Bucket*>::iterator ik = lower_bound(bucket_set, bucket_to_be_deleted->key());

  if ( ik == bucket_set.end() ) {
    throw std::logic_error(std::string(method));
  }

  if ( bucket_to_be_deleted != *ik ) {
    throw std::logic_error(std::string(method));
  }

  ik = bucket_set.erase( ik );

  if ( first != bucket_to_be_deleted ) {

    if ( ik == bucket_set.begin() ) {
      throw std::logic_error(std::string(method));
    }

    first->m_bucketImpl.set_last_bucket_in_family( *--ik );

    if ( 0 == first->m_bucketImpl.get_bucket_family_pointer()->size() ) {
      throw std::logic_error(std::string(method));
    }
  }

  destroy_bucket( bucket_to_be_deleted );
}

//----------------------------------------------------------------------
void BucketRepository::destroy_bucket( Bucket * bucket )
{
  bucket->~Bucket();
  std::free( bucket );
}

//
//----------------------------------------------------------------------
// The input part ordinals are complete and contain all supersets.
void
BucketRepository::declare_nil_bucket()
{
  if (m_nil_bucket == NULL) {
    unsigned field_count = m_mesh.mesh_meta_data().get_fields().size();

    //----------------------------------
    // Field map gives NULL for all field data.

    impl::BucketImpl::DataMap * field_map =
      reinterpret_cast<impl::BucketImpl::DataMap*>(
        local_malloc( sizeof(impl::BucketImpl::DataMap) * ( field_count + 1 )));

    const FieldBase::Restriction & dim = empty_field_restriction();

    for ( unsigned i = 0 ; i < field_count ; ++i ) {
      field_map[ i ].m_base = 0 ;
      field_map[ i ].m_size = 0 ;
      field_map[ i ].m_stride = dim.stride ;
    }
    field_map[ field_count ].m_base   = 0 ;
    field_map[ field_count ].m_size   = 0 ;
    field_map[ field_count ].m_stride = NULL ;

    //----------------------------------
    // Allocation size:  sizeof(Bucket) + key_size * sizeof(unsigned);

    const unsigned alloc_size = align( sizeof(Bucket) ) +
                                align( sizeof(unsigned) * 2 );

    // All fields checked and sized, Ready to allocate

    void * const alloc_ptr = local_malloc( alloc_size );

    unsigned char * ptr = reinterpret_cast<unsigned char *>( alloc_ptr );

    ptr += align( sizeof( Bucket ) );

    unsigned * const new_key = reinterpret_cast<unsigned *>( ptr );

    // Key layout:
    // { part_count + 1 , { part_ordinals } , family_count }

    new_key[0] = 1 ; // part_count + 1
    new_key[1] = 0 ; // family_count

    const unsigned bad_entity_rank = ~0u ;

    Bucket * bucket =
      new( alloc_ptr ) Bucket( m_mesh , bad_entity_rank , new_key ,
                              alloc_size , 0 , field_map , NULL );

    bucket->m_bucketImpl.set_bucket_family_pointer( bucket );

    //----------------------------------

    m_nil_bucket = bucket;
  }
}


//----------------------------------------------------------------------
// The input part ordinals are complete and contain all supersets.
Bucket *
BucketRepository::declare_bucket(
                        const unsigned arg_entity_rank ,
                        const unsigned part_count ,
                        const unsigned part_ord[] ,
                        const std::vector< FieldBase * > & field_set
                              )
{
  enum { KEY_TMP_BUFFER_SIZE = 64 };

  static const char method[] = "stk::mesh::impl::BucketRepository::declare_bucket" ;

  const unsigned max = ~(0u);
  const size_t   num_fields = field_set.size();

  m_mesh.mesh_meta_data().assert_entity_rank( method, arg_entity_rank );
  std::vector<Bucket *> & bucket_set = m_buckets[ arg_entity_rank ];

  //----------------------------------
  // For performance try not to allocate a temporary.

  unsigned key_tmp_buffer[ KEY_TMP_BUFFER_SIZE ];

  std::vector<unsigned> key_tmp_vector ;

  const unsigned key_size = 2 + part_count ;

  unsigned * const key =
    ( key_size <= KEY_TMP_BUFFER_SIZE )
    ? key_tmp_buffer
    : ( key_tmp_vector.resize( key_size ) , & key_tmp_vector[0] );

  //----------------------------------
  // Key layout:
  // { part_count + 1 , { part_ordinals } , family_count }
  // Thus family_count = key[ key[0] ]
  //
  // for upper bound search use the maximum key.

  key[ key[0] = part_count + 1 ] = max ;

  {
    unsigned * const k = key + 1 ;
    for ( unsigned i = 0 ; i < part_count ; ++i ) { k[i] = part_ord[i] ; }
  }

  //----------------------------------
  // Bucket family has all of the same parts.
  // Look for the last bucket in this family:

  const std::vector<Bucket*>::iterator ik = lower_bound( bucket_set , key );

  //----------------------------------
  // If a member of the bucket family has space, it is the last one
  // since buckets are kept packed.
  const bool bucket_family_exists =
    ik != bucket_set.begin() && bucket_part_equal( ik[-1]->key() , key );

  Bucket * const last_bucket = bucket_family_exists ? ik[-1] : NULL ;

  Bucket          * bucket    = NULL ;
  impl::BucketImpl::DataMap * field_map = NULL ;

  if ( last_bucket == NULL ) { // First bucket in this family
    key[ key[0] ] = 0 ; // Set the key's family count to zero
  }
  else { // Last bucket present, can it hold one more entity?

    if ( 0 == last_bucket->size() ) {
      throw std::logic_error( std::string(method) );
    }

    field_map = last_bucket->m_bucketImpl.get_field_map();

    const unsigned last_count = last_bucket->key()[ key[0] ];

    const unsigned cap = last_bucket->capacity();

    if ( last_bucket->size() < cap ) {
      bucket = last_bucket ;
    }
    else if ( last_count < max ) {
      key[ key[0] ] = 1 + last_count ; // Increment the key's family count.
    }
    else {
      // ERROR insane number of buckets!
      std::string msg ;
      msg.append( method );
      msg.append( " FAILED due to insanely large number of buckets" );
      throw std::logic_error( msg );
    }
  }

  //----------------------------------
  // Family's field map does not exist, create it:

  if ( NULL == field_map ) {

    field_map = reinterpret_cast<impl::BucketImpl::DataMap*>(
                local_malloc( sizeof(impl::BucketImpl::DataMap) * ( num_fields + 1 )));

    // Start field data memory after the array of member entity pointers:
    unsigned value_offset = align( sizeof(Entity*) * m_bucket_capacity );

    for ( unsigned i = 0 ; i < num_fields ; ++i ) {
      const FieldBase  & field = * field_set[i] ;

      unsigned value_size = 0 ;

      const FieldBase::Restriction & dim =
        dimension( field, arg_entity_rank, part_count, part_ord, method);

      if ( dim.stride[0] ) { // Exists

        const unsigned type_stride = field.data_traits().stride_of ;
        const unsigned field_rank  = field.rank();

        value_size = type_stride *
          ( field_rank ? dim.stride[ field_rank - 1 ] : 1 );
      }

      field_map[i].m_base = value_offset ;
      field_map[i].m_size = value_size ;
      field_map[i].m_stride = dim.stride ;

      value_offset += align( value_size * m_bucket_capacity );
    }
    field_map[ num_fields ].m_base  = value_offset ;
    field_map[ num_fields ].m_size = 0 ;
    field_map[ num_fields ].m_stride = NULL ;
  }

  //----------------------------------

  if ( NULL == bucket ) {

    // Required bucket does not exist, must allocate and insert
    //
    // Allocation size:
    //   sizeof(Bucket) +
    //   key_size * sizeof(unsigned) +
    //   sizeof(Entity*) * capacity() +
    //   sum[number_of_fields]( fieldsize * capacity )
    //
    // The field_map[ num_fields ].m_base spans
    //   sizeof(Entity*) * capacity() +
    //   sum[number_of_fields]( fieldsize * capacity )

    const unsigned alloc_size = align( sizeof(Bucket) ) +
                                align( sizeof(unsigned) * key_size ) +
                                field_map[ num_fields ].m_base ;

    // All fields checked and sized, Ready to allocate

    void * const alloc_ptr = local_malloc( alloc_size );

    unsigned char * ptr = reinterpret_cast<unsigned char *>( alloc_ptr );

    ptr += align( sizeof( Bucket ) );

    unsigned * const new_key = reinterpret_cast<unsigned *>( ptr );

    ptr += align( sizeof(unsigned) * key_size );

    Entity ** const entity_array = reinterpret_cast<Entity**>( ptr );

    for ( unsigned i = 0 ; i < key_size ; ++i ) { new_key[i] = key[i] ; }

    bucket = new( alloc_ptr ) Bucket( m_mesh, arg_entity_rank , new_key,
                                      alloc_size, m_bucket_capacity ,
                                      field_map , entity_array );

    Bucket * first_bucket = last_bucket ? last_bucket->m_bucketImpl.first_bucket_in_family() : bucket ;

    bucket->m_bucketImpl.set_first_bucket_in_family(first_bucket); // Family members point to first bucket

    first_bucket->m_bucketImpl.set_last_bucket_in_family(bucket); // First bucket points to new last bucket

    bucket_set.insert( ik , bucket );
  }

  //----------------------------------

  return bucket ;
}



//----------------------------------------------------------------------

void BucketRepository::zero_fields( Bucket & k_dst , unsigned i_dst )
{
  k_dst.m_bucketImpl.zero_fields(i_dst);
}

void BucketRepository::copy_fields( Bucket & k_dst , unsigned i_dst ,
                          Bucket & k_src , unsigned i_src )
{
  k_dst.m_bucketImpl.replace_fields(i_dst,k_src,i_src);
}

//----------------------------------------------------------------------

void BucketRepository::update_field_data_states() const
{
  for ( std::vector< std::vector<Bucket*> >::const_iterator
        i = m_buckets.begin() ; i != m_buckets.end() ; ++i ) {

    const std::vector<Bucket*> & kset = *i ;

    for ( std::vector<Bucket*>::const_iterator
          ik = kset.begin() ; ik != kset.end() ; ++ik ) {
      (*ik)->m_bucketImpl.update_state();
    }
  }
}


//----------------------------------------------------------------------

const std::vector<Bucket*> & BucketRepository::buckets( unsigned type ) const
{
  static const char method[]= "stk::mesh::impl::BucketRepository::buckets" ;

  m_mesh.mesh_meta_data().assert_entity_rank( method , type );

  return m_buckets[ type ];
}

//----------------------------------------------------------------------


void BucketRepository::internal_sort_bucket_entities()
{
  for ( unsigned entity_rank = 0 ;
                 entity_rank < m_buckets.size() ; ++entity_rank ) {

    std::vector<Bucket*> & buckets = m_buckets[ entity_rank ];

    size_t bk = 0 ; // Offset to first bucket of the family
    size_t ek = 0 ; // Offset to end   bucket of the family

    for ( ; bk < buckets.size() ; bk = ek ) {
      Bucket * b_scratch = NULL ;
      Bucket * ik_vacant = buckets[bk]->m_bucketImpl.last_bucket_in_family();
      unsigned ie_vacant = ik_vacant->size();

      if ( ik_vacant->capacity() <= ie_vacant ) {
        // Have to create a bucket just for the scratch space...
        const unsigned * const bucket_key = buckets[bk]->key() ;
        const unsigned         part_count = bucket_key[0] - 1 ;
        const unsigned * const part_ord   = bucket_key + 1 ;

        b_scratch = declare_bucket( entity_rank ,
            part_count , part_ord ,
            m_mesh.mesh_meta_data().get_fields() );

        ik_vacant = b_scratch ;
        ie_vacant = 0 ;
      }

      ik_vacant->m_bucketImpl.replace_entity( ie_vacant , NULL ) ;

      // Determine offset to the end bucket in this family:
      while ( ek < buckets.size() && ik_vacant != buckets[ek] ) { ++ek ; }
      ++ek ;

      unsigned count = 0 ;
      for ( size_t ik = bk ; ik != ek ; ++ik ) {
        count += buckets[ik]->size();
      }

      std::vector<Entity*> entities( count );

      std::vector<Entity*>::iterator j = entities.begin();

      for ( size_t ik = bk ; ik != ek ; ++ik ) {
        Bucket & b = * buckets[ik];
        const unsigned n = b.size();
        for ( unsigned i = 0 ; i < n ; ++i , ++j ) {
          *j = & b[i] ;
        }
      }

      std::sort( entities.begin() , entities.end() , EntityLess() );

      j = entities.begin();

      bool change_this_family = false ;

      for ( size_t ik = bk ; ik != ek ; ++ik ) {
        Bucket & b = * buckets[ik];
        const unsigned n = b.size();
        for ( unsigned i = 0 ; i < n ; ++i , ++j ) {
          Entity * const current = & b[i] ;

          if ( current != *j ) {

            if ( current ) {
              // Move current entity to the vacant spot
              copy_fields( *ik_vacant , ie_vacant , b, i );
              m_entity_repo.change_entity_bucket(*ik_vacant, *current, ie_vacant);
              ik_vacant->m_bucketImpl.replace_entity( ie_vacant , current ) ;
            }

            // Set the vacant spot to where the required entity is now.
            ik_vacant = & ((*j)->bucket()) ;
            ie_vacant = (*j)->bucket_ordinal() ;
            ik_vacant->m_bucketImpl.replace_entity( ie_vacant , NULL ) ;

            // Move required entity to the required spot
            copy_fields( b, i, *ik_vacant , ie_vacant );
            m_entity_repo.change_entity_bucket( b, **j, i);
            b.m_bucketImpl.replace_entity( i, *j );

            change_this_family = true ;
          }

          // Once a change has occured then need to propagate the
          // relocation for the remainder of the family.
          // This allows the propagation to be performed once per
          // entity as opposed to both times the entity is moved.

          if ( change_this_family ) { internal_propagate_relocation( **j ); }
        }
      }

      if ( b_scratch ) {
        // Created a last bucket, now have to destroy it.
        destroy_bucket( entity_rank , b_scratch );
        --ek ;
      }
    }
  }
}

//----------------------------------------------------------------------

void BucketRepository::remove_entity( Bucket * k , unsigned i )
{
  const unsigned entity_rank = k->entity_rank();

  // Last bucket in the family of buckets with the same parts.
  // The last bucket is the only non-full bucket in the family.

  Bucket * const last = k->m_bucketImpl.last_bucket_in_family();

  // Fill in the gap if it is not the last entity being removed

  if ( last != k || k->size() != i + 1 ) {

    // Copy last entity in last bucket to bucket *k slot i

    Entity & entity = (*last)[ last->size() - 1 ];

    copy_fields( *k , i , *last , last->size() - 1 );

    k->m_bucketImpl.replace_entity(i, & entity ) ;
    m_entity_repo.change_entity_bucket( *k, entity, i);

    // Entity field data has relocated

    internal_propagate_relocation( entity );
  }

  last->m_bucketImpl.decrement_size();

  last->m_bucketImpl.replace_entity( last->size() , NULL ) ;

  if ( 0 == last->size() ) {
    destroy_bucket( entity_rank , last );
  }
}

//----------------------------------------------------------------------

void BucketRepository::internal_propagate_relocation( Entity & entity )
{
  const unsigned etype = entity.entity_rank();
  PairIterRelation rel = entity.relations();

  for ( ; ! rel.empty() ; ++rel ) {
    const unsigned rel_type = rel->entity_rank();
    if ( rel_type < etype ) {
      Entity & e_to = * rel->entity();

      set_field_relations( entity, e_to, rel->identifier() );
    }
    else if ( etype < rel_type ) {
      Entity & e_from = * rel->entity();

      set_field_relations( e_from, entity, rel->identifier() );
    }
  }
}


} // namespace impl
} // namespace mesh
} // namespace stk


