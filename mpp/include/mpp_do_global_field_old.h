    subroutine MPP_DO_GLOBAL_FIELD_3Dold_( domain, local, global, tile, flags)
!get a global field from a local field
!local field may be on compute OR data domain
      type(domain2D), intent(in)    :: domain
      MPP_TYPE_, intent(in)         ::  local(:,:,:)
      integer, intent(in)           :: tile
      MPP_TYPE_, intent(out)        :: global(domain%x(tile)%global%begin:,domain%y(tile)%global%begin:,:)
      integer, intent(in), optional :: flags


      integer :: i, j, k, m, n, nd, nwords, lpos, rpos, ioff, joff, from_pe, root_pe, tile_id
      integer :: ke, isc, iec, jsc, jec, is, ie, js, je, nword_me
      logical :: xonly, yonly
      MPP_TYPE_ :: clocal ((domain%x(1)%compute%size)    *(domain%y(1)%compute%size)    *size(local,3))
      MPP_TYPE_ :: cremote((domain%x(1)%compute%max_size)*(domain%y(1)%compute%max_size)*size(local,3))
      integer :: stackuse
      character(len=8) :: text
#ifdef use_CRI_pointers
      pointer( ptr_local,  clocal  ) 
      pointer( ptr_remote, cremote )

      ptr_local  = LOC(mpp_domains_stack)
      ptr_remote = LOC(mpp_domains_stack(size(clocal(:))+1))
#endif

      if( .NOT.module_is_initialized )call mpp_error( FATAL, 'MPP_GLOBAL_FIELD: must first call mpp_domains_init.' )

      xonly = .FALSE.
      yonly = .FALSE.
      if( PRESENT(flags) )then
          xonly = flags.EQ.XUPDATE
          yonly = flags.EQ.YUPDATE
          if( .NOT.xonly .AND. .NOT.yonly )call mpp_error( WARNING, 'MPP_GLOBAL_FIELD: you must have flags=XUPDATE or YUPDATE.' )
      end if

      if( size(global,1).NE.(domain%x(tile)%global%size) .OR. size(global,2).NE.(domain%y(tile)%global%size) .OR. &
           size(local,3).NE.size(global,3) ) &
           call mpp_error( FATAL, 'MPP_GLOBAL_FIELD: incoming arrays do not match domain.' )
      if( size(local,1).EQ.(domain%x(tile)%compute%size) .AND. size(local,2).EQ.(domain%y(tile)%compute%size) )then
         !local is on compute domain
         ioff = -domain%x(tile)%compute%begin + 1
         joff = -domain%y(tile)%compute%begin + 1
      else if( size(local,1).EQ.(domain%x(tile)%memory%size) .AND. size(local,2).EQ.(domain%y(tile)%memory%size) )then
         !local is on data domain
         ioff = -domain%x(tile)%data%begin + 1
         joff = -domain%y(tile)%data%begin + 1
      else
         call mpp_error( FATAL, 'MPP_GLOBAL_FIELD_: incoming field array must match either compute domain or memory domain.' )
      end if

      ke  = size(local,3)
      isc = domain%x(tile)%compute%begin; iec = domain%x(tile)%compute%end
      jsc = domain%y(tile)%compute%begin; jec = domain%y(tile)%compute%end

      nword_me = (iec-isc+1)*(jec-jsc+1)*ke

! make contiguous array from compute domain
      m = 0
      do k = 1, ke
         do j = jsc, jec
            do i = isc, iec
               m = m + 1
               clocal(m) = local(i+ioff,j+joff,k)
               global(i,j,k) = clocal(m) !always fill local domain directly
            end do
         end do
      end do

! if there is more than one tile on this pe, then no decomposition for all tiles on this pe, so we can just return
      if(size(domain%x(:))>1) then
         !--- the following is needed to avoid deadlock.
         if( tile == size(domain%x(:)) ) call mpp_sync_self( )
         return
      end if

      root_pe = mpp_root_pe()

      stackuse = size(clocal(:))+size(cremote(:))
      if( stackuse.GT.mpp_domains_stack_size )then
          write( text, '(i8)' )stackuse
          call mpp_error( FATAL, &
               'MPP_UPDATE_DOMAINS user stack overflow: call mpp_domains_set_stack_size('//trim(text)//') from all PEs.' )
      end if
      mpp_domains_stack_hwm = max( mpp_domains_stack_hwm, stackuse )


!fill off-domains (note loops begin at an offset of 1)
      if( xonly )then
          nd = size(domain%x(1)%list(:))
          do n = 1,nd-1
             lpos = mod(domain%x(1)%pos+nd-n,nd)
             rpos = mod(domain%x(1)%pos   +n,nd)
             from_pe = domain%x(1)%list(rpos)%pe
             rpos = from_pe - root_pe ! for concurrent run, root_pe may not be 0.
             nwords = (domain%list(rpos)%x(1)%compute%size) * (domain%list(rpos)%y(1)%compute%size) * ke
           ! Force use of scalar, integer ptr interface
             call mpp_transmit( put_data=clocal(1), plen=nword_me, to_pe=domain%x(1)%list(lpos)%pe, &
                                get_data=cremote(1), glen=nwords, from_pe=from_pe )
             m = 0
             is = domain%list(from_pe)%x(1)%compute%begin; ie = domain%list(from_pe)%x(1)%compute%end
             do k = 1, ke
                do j = jsc, jec
                   do i = is, ie
                      m = m + 1
                      global(i,j,k) = cremote(m)
                   end do
                end do
             end do
          end do
      else if( yonly )then
          nd = size(domain%y(1)%list(:))
          do n = 1,nd-1
             lpos = mod(domain%y(1)%pos+nd-n,nd)
             rpos = mod(domain%y(1)%pos   +n,nd)
             from_pe = domain%y(1)%list(rpos)%pe
             rpos = from_pe - root_pe
             nwords = (domain%list(rpos)%x(1)%compute%size) &
                    * (domain%list(rpos)%y(1)%compute%size) * ke
           ! Force use of scalar, integer pointer interface
             call mpp_transmit( put_data=clocal(1), plen=nword_me, to_pe=domain%y(1)%list(lpos)%pe, &
                                get_data=cremote(1), glen=nwords, from_pe=from_pe )
             m = 0
             js = domain%list(from_pe)%y(1)%compute%begin; je = domain%list(from_pe)%y(1)%compute%end
             do k = 1,ke
                do j = js, je
                   do i = isc, iec
                      m = m + 1
                      global(i,j,k) = cremote(m)
                   end do
                end do
             end do
          end do
      else
          tile_id = domain%tile_id(1)
          nd = size(domain%list(:))
          do n = 1,nd-1
             lpos = mod(domain%pos+nd-n,nd)
             if( domain%list(lpos)%tile_id(1).NE. tile_id ) cycle ! global field only within tile
             call mpp_send( clocal(1), plen=nword_me, to_pe=domain%list(lpos)%pe )
          end do
          do n = 1,nd-1
             rpos = mod(domain%pos+n,nd)
             if( domain%list(rpos)%tile_id(1) .NE. tile_id ) cycle ! global field only within tile
             nwords = (domain%list(rpos)%x(1)%compute%size) * (domain%list(rpos)%y(1)%compute%size) * ke
             call mpp_recv( cremote(1), glen=nwords, from_pe=domain%list(rpos)%pe )
             m = 0
             is = domain%list(rpos)%x(1)%compute%begin; ie = domain%list(rpos)%x(1)%compute%end
             js = domain%list(rpos)%y(1)%compute%begin; je = domain%list(rpos)%y(1)%compute%end
             
             do k = 1,ke
                do j = js, je
                   do i = is, ie
                      m = m + 1
                      global(i,j,k) = cremote(m)
                   end do
                end do
             end do
          end do
      end if

      call mpp_sync_self()
          
      return
    end subroutine MPP_DO_GLOBAL_FIELD_3Dold_
