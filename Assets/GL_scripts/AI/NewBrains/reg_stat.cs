using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public unsafe class reg_stat : MonoBehaviour {

	public regStat thisregiment;

	//thisregiment = mainStatic.regStat[567];

	public regStat tablereg;

	public int number;

	// Номер части в которую определили
	public int part_number; 

	// Сюда сложим приказы
	public Vector2 new_pos;

	/// Номер состояния
	public int sost;

	// Номер состояния по поворотам
	public int rot_sost;

	//  Номер состояния по стрельбе
	public int arch_sost;


	// Список приказов, которые пришли
	public List <reg_command> commands;

	public unsafe reg_stat ()
	{
		thisregiment = & mainStatic.regiment_stat [number];
	}

	public void sost_0 ()
	{
		/// Прост стоим
	}

	public void sost_1 ()
	{

		/// ВРЕМЕННО
		int num = 0; int of_num = 0;
		 
		/// Случай движения и атаки
		int t = func.getRegNear (num, of_num);
		int r = 0;
		if (t != 99999) {
			r = mainStatic.makeRegimentRotation (num, mainStatic.regiment_stat [t].regPosition.x, mainStatic.regiment_stat [t].regPosition.z);
		}
		if (r == 1) {
			mainStatic.makeRegimentMove (num, 4.0f);
			mainStatic.updateRegimentPosition (num);
		}

	}

	public void sost_2 ()
	{
		/// ВРЕМЕННО
		int num = 0; int of_num = 0;
		/// Случай просто движение вперед
		mainStatic.makeRegimentMove(num, 4.0f);
		mainStatic.updateRegimentPosition(num);
	}

	public void sost_rot_0 ()
	{

	}

	public void sost_rot_1 ()
	{

	}

	public void sost_arch_0 ()
	{

	}

}
